#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace swbfo::extract {

enum class SourceKind {
    bf2,              // Battlefront II (2005): its GameData\Data\_lvl_pc
    bf2_unsupported,  // a Battlefront the extractor can't use (Classic Collection)
    swg_archives,     // a folder of readable Galaxies .tre archives
    swg_cabs,         // InstallShield cabinets (data1.hdr + data*.cab): the Total Experience discs
    swg_encrypted,    // SWG Restoration's archives: encrypted, never read
};

const char* kind_name(SourceKind kind);

struct FoundSource {
    SourceKind kind = SourceKind::bf2;
    std::filesystem::path path;  // the _lvl_pc, archive or cabinet folder
    std::string found_by;        // "Steam", "GOG", "installed programs", "known folder", "disc", "drive search", "chosen"
    std::string note;            // for the player: what it is, why it can't be used
    size_t archives = 0;         // swg_archives: readable archives in the folder
};

// What a folder is: a Battlefront II install (the folder itself, or its game
// folder: GameData\Data\_lvl_pc is looked for), Galaxies archives, cabinets,
// or nothing (empty). Checks the folder and its immediate subfolders.
std::vector<FoundSource> classify_folder(const std::filesystem::path& dir, const std::string& found_by = "chosen");

struct SearchOptions {
    // Also walk every fixed, removable and optical drive (minutes). Skips
    // Windows, the recycle bin and other system folders.
    bool full = false;
    const std::atomic<bool>* cancel = nullptr;
    // Called now and then during a full search: the folder being read and
    // the number of folders read so far.
    std::function<void(const std::filesystem::path&, size_t)> on_progress;
};

// Finds installs: Steam libraries, GOG, installed programs, known folders and
// discs, then (full) every drive. Reads only folder listings, the registry and
// archive headers. Each folder appears once.
std::vector<FoundSource> find_sources(const SearchOptions& options);

// Writes a .sources file (swgtool's: a reference manifest and the folders to
// read Galaxies files from).
void write_sources_file(const std::filesystem::path& file, const std::filesystem::path& manifest,
                        const std::vector<std::filesystem::path>& folders);

} // namespace swbfo::extract
