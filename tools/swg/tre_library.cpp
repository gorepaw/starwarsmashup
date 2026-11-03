#include "swg/tre_library.h"

#include <cctype>
#include <stdexcept>
#include <tuple>

#include "swg/manifest.h"
#include "swg/tre_scan.h"

namespace swm::swg {
namespace {

// (tier, patch level): bottom < data < default_patch < patch_NN.
std::pair<int, int> archive_rank(std::string_view archive_name) {
    const std::string name = normalize_path(archive_name.substr(archive_name.find_last_of('/') + 1));
    if (name.starts_with("bottom")) {
        return {0, 0};
    }
    if (name.starts_with("default_patch")) {
        return {2, 0};
    }
    if (!name.starts_with("patch_")) {
        return {1, 0};
    }
    // patch_NN_..., or patch_skuN_NN_...: the level is the first number after the sku tag.
    size_t pos = 6;
    if (name.compare(pos, 3, "sku") == 0) {
        pos = name.find('_', pos);
        pos = pos == std::string::npos ? name.size() : pos + 1;
    }
    int level = 0;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
        level = level * 10 + (name[pos++] - '0');
    }
    return {3, level};
}

} // namespace

bool TreLibrary::higher_priority(std::string_view a, std::string_view b) {
    return std::tuple(archive_rank(a), a) > std::tuple(archive_rank(b), b);
}

TreLibrary::TreLibrary(const std::filesystem::path& dir) {
    for (const auto& path : find_tre_archives(dir)) {
        archives_.push_back(std::make_unique<TreArchive>(path));
        archive_names_.push_back(std::filesystem::relative(path, dir).generic_string());
        const size_t archive = archives_.size() - 1;
        const auto& entries = archives_.back()->entries();
        for (size_t entry = 0; entry < entries.size(); ++entry) {
            const std::string key = normalize_path(entries[entry].path);
            const auto existing = index_.find(key);
            if (existing == index_.end() ||
                higher_priority(archive_names_[archive], archive_names_[existing->second.archive])) {
                index_[key] = {archive, entry};
            }
        }
    }
}

bool TreLibrary::contains(std::string_view path) const {
    return index_.contains(normalize_path(path));
}

std::vector<uint8_t> TreLibrary::read(std::string_view path) {
    const auto it = index_.find(normalize_path(path));
    if (it == index_.end()) {
        throw std::runtime_error("not found in any archive: " + std::string(path));
    }
    TreArchive& archive = *archives_[it->second.archive];
    return archive.read(archive.entries()[it->second.entry]);
}

std::string TreLibrary::archive_of(std::string_view path) const {
    const auto it = index_.find(normalize_path(path));
    return it == index_.end() ? std::string() : archive_names_[it->second.archive];
}

} // namespace swm::swg
