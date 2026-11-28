#include "swbfo/core/telemetry.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>

namespace swbfo::core {

namespace {

std::string lower(std::string_view text) {
    std::string out;
    for (const char c : text) out += char(std::tolower(uint8_t(c)));
    return out;
}

} // namespace

int MatchTelemetry::player_count() const {
    return int(std::count_if(participants.begin(), participants.end(),
                             [](const MatchParticipant& p) { return !p.bot && p.team >= 0 && p.team < 2; }));
}

int MatchTelemetry::bot_count() const {
    return int(std::count_if(participants.begin(), participants.end(),
                             [](const MatchParticipant& p) { return p.bot && p.team >= 0 && p.team < 2; }));
}

std::string era_of(const std::array<std::string, 2>& teams) {
    for (const std::string& team : teams) {
        const std::string name = lower(team);
        if (name.find("republic") != std::string::npos || name.find("separatist") != std::string::npos ||
            name.find("clone") != std::string::npos || name == "cis") {
            return "cw";
        }
    }
    return "gcw";
}

std::string encode_weapon_kills(const std::map<std::string, uint16_t>& kills) {
    std::string out;
    for (const auto& [weapon, count] : kills) {
        if (weapon.empty() || weapon.find_first_of("=;") != std::string::npos) {
            continue;
        }
        out += (out.empty() ? "" : ";") + weapon + "=" + std::to_string(count);
    }
    return out;
}

std::map<std::string, uint16_t> decode_weapon_kills(const std::string& text) {
    std::map<std::string, uint16_t> kills;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, ';');) {
        const size_t eq = part.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        kills[part.substr(0, eq)] = uint16_t(std::clamp(std::atoi(part.c_str() + eq + 1), 0, 65535));
    }
    return kills;
}

TelemetryReport summarize_telemetry(std::span<const MatchTelemetry> matches) {
    TelemetryReport report;
    std::map<std::string, WinStats> factions;
    std::map<std::string, WinStats> modes;
    std::map<std::string, MapStats> maps;
    std::map<std::string, WeaponStats> weapons;
    int player_kills = 0, player_deaths = 0, bot_kills = 0, bot_deaths = 0;
    double duration = 0;
    for (const MatchTelemetry& m : matches) {
        ++report.matches;
        duration += m.duration;
        report.player_slots += m.player_count();
        report.bot_slots += m.bot_count();
        for (int side = 0; side < 2; ++side) {
            WinStats& f = factions[m.teams[size_t(side)]];
            f.key = m.teams[size_t(side)];
            ++f.matches;
            if (m.winner < 0) ++f.draws;
            else if (m.winner == side) ++f.wins;
            else ++f.losses;
        }
        WinStats& mode = modes[m.mode];
        mode.key = m.mode;
        ++mode.matches;
        if (m.winner < 0) ++mode.draws;
        else if (m.winner == 0) ++mode.wins;
        else ++mode.losses;
        MapStats& map = maps[m.map + "|" + m.mode + "|" + m.era];
        map.map = m.map;
        map.mode = m.mode;
        map.era = m.era;
        map.teams = m.teams;
        ++map.matches;
        if (m.winner < 0) ++map.draws;
        else ++map.wins[size_t(m.winner)];
        map.average_duration += m.duration;
        map.average_tickets[0] += m.tickets[0];
        map.average_tickets[1] += m.tickets[1];
        for (const MatchParticipant& p : m.participants) {
            if (p.team < 0 || p.team > 1) continue;
            (p.bot ? bot_kills : player_kills) += p.kills;
            (p.bot ? bot_deaths : player_deaths) += p.deaths;
            if (!p.primary_weapon.empty()) {
                WeaponStats& w = weapons[p.primary_weapon];
                w.weapon = p.primary_weapon;
                ++w.carried;
            }
            for (const auto& [weapon, kills] : p.weapon_kills) {
                WeaponStats& w = weapons[weapon];
                w.weapon = weapon;
                w.kills += kills;
            }
        }
    }
    report.average_duration = report.matches > 0 ? duration / report.matches : 0;
    for (auto& [key, f] : factions) report.factions.push_back(f);
    for (auto& [key, m] : modes) report.modes.push_back(m);
    for (auto& [key, m] : maps) {
        m.average_duration /= m.matches;
        m.average_tickets[0] /= m.matches;
        m.average_tickets[1] /= m.matches;
        report.maps.push_back(m);
    }
    for (auto& [key, w] : weapons) report.weapons.push_back(w);
    std::sort(report.factions.begin(), report.factions.end(),
              [](const WinStats& a, const WinStats& b) { return a.win_rate() > b.win_rate(); });
    std::sort(report.weapons.begin(), report.weapons.end(),
              [](const WeaponStats& a, const WeaponStats& b) { return a.kills > b.kills; });
    report.player_kd = player_deaths > 0 ? double(player_kills) / player_deaths : double(player_kills);
    report.bot_kd = bot_deaths > 0 ? double(bot_kills) / bot_deaths : double(bot_kills);
    return report;
}

std::string format_telemetry_report(const TelemetryReport& r) {
    std::string out = std::format("Match telemetry: {} matches, {} player and {} bot slots, {:.0f} s on average\n",
                                  r.matches, r.player_slots, r.bot_slots, r.average_duration);
    out += std::format("K/D: players {:.2f}, bots {:.2f}\n", r.player_kd, r.bot_kd);
    out += "\nFactions (win rate):\n";
    for (const WinStats& f : r.factions) {
        out += std::format("  {:<14} {:5.1f}%  {} W / {} L / {} D of {}\n", f.key, f.win_rate() * 100, f.wins,
                           f.losses, f.draws, f.matches);
    }
    out += "\nModes (first side's win rate):\n";
    for (const WinStats& m : r.modes) {
        out += std::format("  {:<10} {:5.1f}%  of {}\n", m.key, m.win_rate() * 100, m.matches);
    }
    out += "\nMaps:\n";
    for (const MapStats& m : r.maps) {
        out += std::format("  {:<20} {:<8} {:<3} x{:<3} {} {} - {} {} (draws {}), {:.0f} s, tickets left {:.0f} / {:.0f}\n",
                           m.map, m.mode, m.era, m.matches, m.teams[0], m.wins[0], m.wins[1], m.teams[1], m.draws,
                           m.average_duration, m.average_tickets[0], m.average_tickets[1]);
    }
    out += "\nWeapons (kills, carried):\n";
    for (size_t i = 0; i < r.weapons.size() && i < 20; ++i) {
        out += std::format("  {:<32} {:>5} kills, carried {} times\n", r.weapons[i].weapon, r.weapons[i].kills,
                           r.weapons[i].carried);
    }
    return out;
}

} // namespace swbfo::core
