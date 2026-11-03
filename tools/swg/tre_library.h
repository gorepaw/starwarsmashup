#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "swg/tre_archive.h"

namespace swm::swg {

// Every TRE archive under a directory, looked up by file path. When several
// archives contain the same path, the highest-priority copy wins.
class TreLibrary {
public:
    explicit TreLibrary(const std::filesystem::path& dir);

    bool contains(std::string_view path) const;

    // Every path in the library, sorted.
    std::vector<std::string> paths() const;

    // Throws std::runtime_error if the path isn't in any archive.
    std::vector<uint8_t> read(std::string_view path);

    // Name of the archive that provides `path`, or empty.
    std::string archive_of(std::string_view path) const;

    // PROVISIONAL until the client's real search order is determined: patch
    // archives beat data archives, higher patch levels beat lower ones, and
    // ties go to the later name. Returns true if `a` beats `b`.
    static bool higher_priority(std::string_view a, std::string_view b);

private:
    struct Location {
        size_t archive = 0;
        size_t entry = 0;
    };

    std::vector<std::unique_ptr<TreArchive>> archives_;
    std::vector<std::string> archive_names_;
    std::unordered_map<std::string, Location> index_;
};

} // namespace swm::swg
