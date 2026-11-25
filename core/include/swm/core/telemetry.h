#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace swm::core {

// Match telemetry (Stage 11): what a finished battle looked like, kept so
// faction, map and mode balance can be read off many of them. Hubs record
// one per battle (CharacterStore::record_match); `swm_server --telemetry`
// prints the summary.

struct MatchParticipant {
    std::string name;
    int team = -1;    // 0 or 1: the match's sides (MatchTelemetry::teams); 2: natives
    bool bot = false;
    uint16_t kills = 0, deaths = 0, captures = 0;
    std::string primary_weapon;                    // the loadout's
    std::map<std::string, uint16_t> weapon_kills;  // kills by weapon ("vehicle": from one)
};

struct MatchTelemetry {
    int64_t id = 0;        // the store's; 0 before it's recorded
    int64_t ended_at = 0;  // Unix seconds
    std::string battlefield;  // its battle id (kamino_cw@1flag)
    std::string map;          // its name (Kamino)
    std::string mode;         // core::game_mode_id
    std::string era;          // "gcw" or "cw"
    bool space = false;
    std::array<std::string, 2> teams;  // the sides' names (Rebellion, Empire...)
    int winner = -1;                   // 0 or 1; -1: a draw
    std::array<double, 2> tickets{};   // reinforcements left
    std::array<double, 2> scores{};    // space points, flag captures or Hunt kills
    double duration = 0;               // seconds live
    uint16_t captures = 0;             // posts taken or neutralized, flags captured
    std::vector<MatchParticipant> participants;

    int player_count() const;
    int bot_count() const;
};

// The era of a match between these sides: Clone Wars if either is the
// Republic's or the Separatists', else the Galactic Civil War.
std::string era_of(const std::array<std::string, 2>& teams);

// "name=3;other=1" and back (weapon kills, as stored).
std::string encode_weapon_kills(const std::map<std::string, uint16_t>& kills);
std::map<std::string, uint16_t> decode_weapon_kills(const std::string& text);

// Win/loss counts for something (a faction, a map...).
struct WinStats {
    std::string key;
    int matches = 0, wins = 0, losses = 0, draws = 0;
    double win_rate() const { return matches > 0 ? double(wins) / matches : 0.0; }
};

struct MapStats {
    std::string map;  // its battlefield's name
    std::string mode;
    std::string era;
    int matches = 0;
    std::array<std::string, 2> teams;  // as fought: the first [team] and the second
    std::array<int, 2> wins{};
    int draws = 0;
    double average_duration = 0;       // seconds
    std::array<double, 2> average_tickets{};
};

struct WeaponStats {
    std::string weapon;
    int kills = 0;
    int carried = 0;  // participants whose primary it was
};

struct TelemetryReport {
    int matches = 0;
    int player_slots = 0, bot_slots = 0;
    double average_duration = 0;
    std::vector<WinStats> factions;   // by faction name
    std::vector<WinStats> modes;      // by mode id, wins counted for the first side
    std::vector<MapStats> maps;       // by map, mode and era
    std::vector<WeaponStats> weapons; // by kills, most first
    double player_kd = 0;             // players' (not bots') kills per death
    double bot_kd = 0;
};

TelemetryReport summarize_telemetry(std::span<const MatchTelemetry> matches);
// The report as text, for the server's console.
std::string format_telemetry_report(const TelemetryReport& report);

} // namespace swm::core
