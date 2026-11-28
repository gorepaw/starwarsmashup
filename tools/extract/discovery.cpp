#include "extract/discovery.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "extract/process.h"

namespace fs = std::filesystem;

namespace swbfo::extract {

const char* kind_name(SourceKind kind) {
    switch (kind) {
    case SourceKind::bf2: return "bf2";
    case SourceKind::bf2_unsupported: return "bf2-unsupported";
    case SourceKind::swg_archives: return "swg-archives";
    case SourceKind::swg_cabs: return "swg-cabs";
    case SourceKind::swg_encrypted: return "swg-encrypted";
    }
    return "?";
}

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string lower_utf8(const fs::path& p) {
    return lower(path_utf8(p));
}

bool is_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// "EERT" then the version, stored reversed: "5000" is 0005 (the retail
// discs and SOE's clients), "6000" is 0006.
std::string tre_version(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char header[8] = {};
    if (!in.read(header, 8) || std::string(header, 4) != "EERT") {
        return {};
    }
    return std::string{header[7], header[6], header[5], header[4]};
}

std::optional<FoundSource> classify_bf2(const fs::path& dir, const std::string& found_by) {
    for (const fs::path& lvl : {dir, dir / "GameData" / "Data" / "_lvl_pc", dir / "Data" / "_lvl_pc", dir / "_lvl_pc"}) {
        if (!is_file(lvl / "ingame.lvl") || !is_file(lvl / "mission.lvl")) {
            continue;
        }
        FoundSource s;
        s.path = lvl;
        s.found_by = found_by;
        if (lower_utf8(lvl).find("classic collection") != std::string::npos) {
            s.kind = SourceKind::bf2_unsupported;
            s.note = "Star Wars Battlefront Classic Collection (2024) isn't supported yet: it's a different build. "
                     "The 2005 game (Steam, GOG or disc) is.";
        } else {
            s.kind = SourceKind::bf2;
            s.note = "Star Wars Battlefront II (2005)";
        }
        return s;
    }
    return std::nullopt;
}

// Galaxies archives or cabinets directly in `dir`.
std::vector<FoundSource> classify_swg(const fs::path& dir, const std::string& found_by) {
    std::vector<FoundSource> out;
    size_t readable = 0;
    size_t encrypted = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec) || lower_utf8(entry.path().extension()) != ".tre") {
            continue;
        }
        const std::string version = tre_version(entry.path());
        if (version.empty()) {
            continue;
        }
        // Restoration repackages everything as SwgRestoration_*.tre and
        // encrypts the data: never read (docs/ARCHITECTURE.md).
        if (lower_utf8(entry.path().filename()).starts_with("swgrestoration")) {
            ++encrypted;
        } else {
            ++readable;
        }
    }
    if (readable) {
        FoundSource s;
        s.kind = SourceKind::swg_archives;
        s.path = dir;
        s.found_by = found_by;
        s.archives = readable;
        s.note = std::to_string(readable) + " Galaxies archives (only files identical to SOE's are used)";
        out.push_back(s);
    }
    if (encrypted) {
        FoundSource s;
        s.kind = SourceKind::swg_encrypted;
        s.path = dir;
        s.found_by = found_by;
        s.note = "SWG Restoration's archives are encrypted and can't be used. Galaxies files can come from the "
                 "Total Experience discs or another install with SOE's original archives.";
        out.push_back(s);
    }
    if (is_file(dir / "data1.hdr") && is_file(dir / "data1.cab")) {
        FoundSource s;
        s.kind = SourceKind::swg_cabs;
        s.path = dir;
        s.found_by = found_by;
        s.note = "InstallShield cabinets (the Total Experience discs?): their archives are read from these";
        out.push_back(s);
    }
    return out;
}

// Registry string, or empty.
std::wstring reg_string(HKEY root, const std::wstring& key, const wchar_t* value) {
    DWORD size = 0;
    if (RegGetValueW(root, key.c_str(), value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring out(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key.c_str(), value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, out.data(), &size) !=
        ERROR_SUCCESS) {
        return {};
    }
    out.resize(wcsnlen(out.c_str(), out.size()));
    return out;
}

std::vector<std::wstring> reg_subkeys(HKEY root, const std::wstring& key) {
    std::vector<std::wstring> out;
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(h, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        out.emplace_back(name, len);
    }
    RegCloseKey(h);
    return out;
}

// Steam's library folders, from steamapps\libraryfolders.vdf ("path" lines).
std::vector<fs::path> steam_libraries() {
    std::vector<fs::path> out;
    const std::wstring steam = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) {
        return out;
    }
    out.push_back(fs::path(steam));
    std::ifstream vdf(fs::path(steam) / "steamapps" / "libraryfolders.vdf", std::ios::binary);
    std::string line;
    while (std::getline(vdf, line)) {
        const size_t key = line.find("\"path\"");
        if (key == std::string::npos) {
            continue;
        }
        const size_t a = line.find('"', key + 6);
        const size_t b = a == std::string::npos ? a : line.find('"', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        std::string value = line.substr(a + 1, b - a - 1);
        std::string unescaped;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') {
                ++i;
            }
            unescaped += value[i];
        }
        out.push_back(utf8_path(unescaped));
    }
    return out;
}

struct Collector {
    std::vector<FoundSource> found;
    std::set<std::string> seen;  // kind + lowercased path

    void add(std::vector<FoundSource> sources) {
        for (auto& s : sources) {
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(s.path, ec);
            if (!ec) {
                s.path = canonical;
            }
            if (seen.insert(std::string(kind_name(s.kind)) + "|" + lower_utf8(s.path)).second) {
                found.push_back(std::move(s));
            }
        }
    }
    void add(const std::optional<FoundSource>& s) {
        if (s) {
            add(std::vector<FoundSource>{*s});
        }
    }
};

bool skipped_in_walk(const fs::path& dir) {
    static const std::set<std::string> names = {"windows", "$recycle.bin", "system volume information", "$winreagent",
                                                "recovery", "$sysreset", "$windows.~bt", "$windows.~ws", "winsxs",
                                                "node_modules", ".git", "__pycache__", "config.msi", "msocache"};
    return names.contains(lower_utf8(dir.filename()));
}

std::vector<fs::path> drives(bool optical_only) {
    std::vector<fs::path> out;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) {
            continue;
        }
        const std::wstring root = std::wstring(1, wchar_t(L'A' + i)) + L":\\";
        const UINT type = GetDriveTypeW(root.c_str());
        const bool want = optical_only ? type == DRIVE_CDROM
                                       : (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM);
        if (want) {
            out.emplace_back(root);
        }
    }
    return out;
}

} // namespace

std::vector<FoundSource> classify_folder(const fs::path& dir, const std::string& found_by) {
    Collector c;
    if (!is_dir(dir)) {
        return {};
    }
    c.add(classify_bf2(dir, found_by));
    c.add(classify_swg(dir, found_by));
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec) && !entry.is_symlink(ec)) {
            c.add(classify_bf2(entry.path(), found_by));
            c.add(classify_swg(entry.path(), found_by));
        }
    }
    return c.found;
}

std::vector<FoundSource> find_sources(const SearchOptions& options) {
    Collector c;

    // Steam: every library's Battlefront folders.
    for (const fs::path& library : steam_libraries()) {
        std::error_code ec;
        for (const auto& game : fs::directory_iterator(library / "steamapps" / "common", ec)) {
            if (lower_utf8(game.path().filename()).find("battlefront") != std::string::npos) {
                c.add(classify_folder(game.path(), "Steam"));
            }
        }
    }

    // GOG Galaxy's and other installers' records.
    for (const wchar_t* gog : {L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"SOFTWARE\\GOG.com\\Games"}) {
        for (const std::wstring& id : reg_subkeys(HKEY_LOCAL_MACHINE, gog)) {
            const std::wstring key = std::wstring(gog) + L"\\" + id;
            const std::string name = lower(narrow(reg_string(HKEY_LOCAL_MACHINE, key, L"gameName")));
            const std::wstring path = reg_string(HKEY_LOCAL_MACHINE, key, L"path");
            if (!path.empty() && name.find("battlefront") != std::string::npos) {
                c.add(classify_folder(fs::path(path), "GOG"));
            }
        }
    }
    const std::pair<HKEY, const wchar_t*> uninstall[] = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
    };
    for (const auto& [root, base] : uninstall) {
        for (const std::wstring& id : reg_subkeys(root, base)) {
            const std::wstring key = std::wstring(base) + L"\\" + id;
            const std::string name = lower(narrow(reg_string(root, key, L"DisplayName")));
            const bool relevant = name.find("battlefront") != std::string::npos ||
                                  name.find("galaxies") != std::string::npos || name.find("swg") != std::string::npos;
            if (!relevant) {
                continue;
            }
            std::wstring location = reg_string(root, key, L"InstallLocation");
            if (location.empty()) {
                // Some installers record only an icon or uninstaller inside the folder.
                std::wstring icon = reg_string(root, key, L"DisplayIcon");
                icon = icon.substr(0, icon.find(L','));
                if (!icon.empty() && icon.front() == L'"') {
                    icon = icon.substr(1, icon.find(L'"', 1) - 1);
                }
                location = icon.empty() ? icon : fs::path(icon).parent_path().wstring();
            }
            if (!location.empty()) {
                c.add(classify_folder(fs::path(location), "installed programs"));
            }
        }
    }

    // Folders the games' installers and the emulator communities use.
    const std::wstring pf = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                                       L"ProgramFilesDir");
    const std::wstring pf86 = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                                         L"ProgramFilesDir (x86)");
    std::vector<fs::path> known;
    for (const std::wstring& base : {pf, pf86}) {
        if (base.empty()) {
            continue;
        }
        for (const char* sub : {"LucasArts\\Star Wars Battlefront II", "Star Wars Battlefront II", "GOG Galaxy\\Games",
                                "StarWarsGalaxies", "Sony\\Star Wars Galaxies", "SOE\\StarWarsGalaxies",
                                "SWGEmu", "SWG Legends", "SWG Restoration", "Steam\\steamapps\\common"}) {
            known.push_back(fs::path(base) / sub);
        }
    }
    for (const fs::path& root : drives(false)) {
        for (const char* sub : {"SWGEmu", "SWG", "SWG Legends", "StarWarsGalaxies", "Games\\SWGEmu", "Games\\SWG"}) {
            known.push_back(root / sub);
        }
    }
    for (const fs::path& dir : known) {
        if (is_dir(dir)) {
            c.add(classify_folder(dir, "known folder"));
        }
    }

    // Discs in optical drives (and mounted disc images).
    for (const fs::path& root : drives(true)) {
        c.add(classify_folder(root, "disc"));
    }

    if (!options.full) {
        return c.found;
    }
    size_t folders = 0;
    for (const fs::path& root : drives(false)) {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        std::set<std::string> checked;
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (options.cancel && options.cancel->load()) {
                return c.found;
            }
            const auto& entry = *it;
            std::error_code e2;
            if (entry.is_directory(e2)) {
                if (entry.is_symlink(e2) || skipped_in_walk(entry.path())) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (++folders % 500 == 0 && options.on_progress) {
                    options.on_progress(entry.path(), folders);
                }
                continue;
            }
            const std::string name = lower_utf8(entry.path().filename());
            const bool interesting = name == "ingame.lvl" || name == "data1.hdr" ||
                                     (name.size() > 4 && name.ends_with(".tre"));
            if (!interesting) {
                continue;
            }
            const fs::path dir = entry.path().parent_path();
            if (checked.insert(lower_utf8(dir)).second) {
                c.add(classify_bf2(dir, "drive search"));
                c.add(classify_swg(dir, "drive search"));
            }
        }
    }
    return c.found;
}

void write_sources_file(const fs::path& file, const fs::path& manifest, const std::vector<fs::path>& folders) {
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path());
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "# Galaxies sources for swgtool: only files identical to the reference manifest's are read.\n";
    out << "manifest " << path_utf8(manifest) << "\n";
    for (const fs::path& f : folders) {
        out << "source " << path_utf8(f) << "\n";
    }
}

} // namespace swbfo::extract
