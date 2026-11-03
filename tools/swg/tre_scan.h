#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include "swg/tre_archive.h"

namespace swm::swg {

// Every .tre file under `dir`, recursively, sorted.
std::vector<std::filesystem::path> find_tre_archives(const std::filesystem::path& dir);

// Called once per archive entry with its uncompressed contents. Called
// concurrently for different archives and sequentially within one archive.
// Exceptions thrown by the visitor are recorded as errors.
using TreVisitor = std::function<void(size_t archive_index, const std::string& archive_name, const TreEntry& entry,
                                      const std::vector<uint8_t>& data)>;

struct TreVisitResult {
    std::vector<std::string> errors;
};

// Reads every entry of every archive, one archive per thread. `archive_name`
// passed to the visitor is relative to `base_dir`. Progress goes to `log`.
TreVisitResult visit_tre_archives(const std::vector<std::filesystem::path>& archives,
                                  const std::filesystem::path& base_dir, std::ostream& log, const TreVisitor& visitor);

} // namespace swm::swg
