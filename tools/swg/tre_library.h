#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "swg/tre_archive.h"

namespace swbfo::swg {

struct Manifest;

// The copy of a file a reference install loads: the one in its
// highest-priority archive (TreLibrary::higher_priority).
struct ReferenceCopy {
    std::string sha256;
    uint64_t size = 0;
};

// Each path in `manifest`, with the copy its install would load.
std::unordered_map<std::string, ReferenceCopy> reference_copies(const Manifest& manifest);

// A ".sources" file: a reference manifest and the folders to look in.
//
//   # comments
//   manifest C:\...\swg-total-experience-2005.tsv
//   source D:\SWGEmu
//   source E:\swg_cache\total_experience
struct SourcesFile {
    std::filesystem::path manifest;
    std::vector<std::filesystem::path> sources;
};

bool is_sources_file(const std::filesystem::path& path);
// Throws std::runtime_error if it can't be read or names no manifest or source.
SourcesFile read_sources_file(const std::filesystem::path& path);

// Every TRE archive under a directory, looked up by file path. When several
// archives contain the same path, the highest-priority copy wins.
//
// Given a ".sources" file instead, it reads every archive in every source but
// uses only SOE originals (docs/ARCHITECTURE.md, Supported game versions): for
// each path, the copy the reference install loads. Candidates are matched by
// size when indexed and by SHA-256 when read; a modified copy is passed over
// for another, and a path with none is reported when read. Paths the reference
// lacks don't exist. Archives that can't be opened are skipped (warnings()).
class TreLibrary {
public:
    explicit TreLibrary(const std::filesystem::path& dir_or_sources);

    bool contains(std::string_view path) const;

    // Every path in the library, sorted.
    std::vector<std::string> paths() const;

    // Throws std::runtime_error if the path isn't in any archive, or (with a
    // manifest) no copy of it is the SOE original.
    std::vector<uint8_t> read(std::string_view path);

    // Name of the archive that provides `path`, or empty. With a manifest, the
    // highest-priority candidate's (it may still fail verification on read).
    std::string archive_of(std::string_view path) const;

    // True when reading through a reference manifest.
    bool verified() const { return !reference_.empty(); }

    const std::vector<std::string>& warnings() const { return warnings_; }

    // PROVISIONAL until the client's real search order is determined: patch
    // archives beat data archives, higher patch levels beat lower ones, and
    // ties go to the later name. Returns true if `a` beats `b`.
    static bool higher_priority(std::string_view a, std::string_view b);

private:
    struct Location {
        size_t archive = 0;
        size_t entry = 0;
    };

    void add_directory(const std::filesystem::path& dir, bool skip_unreadable);

    std::vector<std::unique_ptr<TreArchive>> archives_;
    std::vector<std::string> archive_names_;
    // Candidates for each path, best first. Without a manifest, only the best.
    std::unordered_map<std::string, std::vector<Location>> index_;
    std::unordered_map<std::string, ReferenceCopy> reference_;
    std::vector<std::string> warnings_;
};

} // namespace swbfo::swg
