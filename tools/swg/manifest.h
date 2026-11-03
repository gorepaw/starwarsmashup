#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace swm::swg {

// One file inside one archive. The same path can appear in several archives
// (later patches override earlier ones), so a manifest keeps every copy.
struct ManifestEntry {
    std::string sha256;   // of the uncompressed contents
    uint64_t size = 0;    // uncompressed
    std::string archive;  // archive path relative to the scanned directory
    std::string path;     // normalized, see normalize_path()
};

// A reference manifest: every file in a known SOE release, by path and hash.
// Stored as TSV with '#' header lines. Contains no game data.
struct Manifest {
    std::string name;
    std::string source;
    std::vector<ManifestEntry> entries;
};

struct ScanResult {
    std::vector<ManifestEntry> entries;
    std::vector<std::string> errors;
    size_t archive_count = 0;
};

// Lowercase, forward slashes, no leading slash.
std::string normalize_path(std::string_view path);

// Hashes every file in every .tre archive under `dir` (recursively), one
// archive per thread. Progress goes to `log`. Entries are sorted by path.
ScanResult scan_tre_directory(const std::filesystem::path& dir, std::ostream& log);

void write_manifest(const Manifest& manifest, std::ostream& out);

// Throws std::runtime_error on malformed input.
Manifest read_manifest(std::istream& in);

} // namespace swm::swg
