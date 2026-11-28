#include "swg/tre_library.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

#include "swg/manifest.h"
#include "swg/sha256.h"
#include "swg/tre_scan.h"

namespace swbfo::swg {
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

std::unordered_map<std::string, ReferenceCopy> reference_copies(const Manifest& manifest) {
    std::unordered_map<std::string, const ManifestEntry*> best;
    for (const ManifestEntry& entry : manifest.entries) {
        const auto [it, inserted] = best.try_emplace(entry.path, &entry);
        if (!inserted && TreLibrary::higher_priority(entry.archive, it->second->archive)) {
            it->second = &entry;
        }
    }
    std::unordered_map<std::string, ReferenceCopy> out;
    out.reserve(best.size());
    for (const auto& [path, entry] : best) {
        out[path] = {entry->sha256, entry->size};
    }
    return out;
}

bool is_sources_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".sources" && std::filesystem::is_regular_file(path);
}

SourcesFile read_sources_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    SourcesFile out;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') {
            continue;
        }
        line.erase(0, start);
        const size_t space = line.find_first_of(" \t");
        const size_t value_start = space == std::string::npos ? space : line.find_first_not_of(" \t", space);
        if (value_start == std::string::npos) {
            throw std::runtime_error(path.string() + ": no value in: " + line);
        }
        const std::string key = line.substr(0, space);
        const std::string text = line.substr(value_start);
        const std::filesystem::path value(std::u8string(text.begin(), text.end()));
        if (key == "manifest") {
            out.manifest = value;
        } else if (key == "source") {
            out.sources.push_back(value);
        } else {
            throw std::runtime_error(path.string() + ": unknown key " + key);
        }
    }
    if (out.manifest.empty() || out.sources.empty()) {
        throw std::runtime_error(path.string() + ": needs a manifest line and at least one source line");
    }
    return out;
}

TreLibrary::TreLibrary(const std::filesystem::path& dir_or_sources) {
    if (!is_sources_file(dir_or_sources)) {
        add_directory(dir_or_sources, false);
        return;
    }
    const SourcesFile sources = read_sources_file(dir_or_sources);
    std::ifstream manifest_file(sources.manifest, std::ios::binary);
    if (!manifest_file) {
        throw std::runtime_error("cannot open manifest " + sources.manifest.string());
    }
    reference_ = reference_copies(read_manifest(manifest_file));
    for (const auto& dir : sources.sources) {
        add_directory(dir, true);
    }
    for (auto& [path, candidates] : index_) {
        std::stable_sort(candidates.begin(), candidates.end(), [&](const Location& a, const Location& b) {
            return higher_priority(archive_names_[a.archive], archive_names_[b.archive]);
        });
    }
    for (const std::string& warning : warnings_) {
        std::cerr << "warning: " << warning << "\n";
    }
}

void TreLibrary::add_directory(const std::filesystem::path& dir, bool skip_unreadable) {
    for (const auto& path : find_tre_archives(dir)) {
        try {
            archives_.push_back(std::make_unique<TreArchive>(path));
        } catch (const std::exception& e) {
            if (!skip_unreadable) {
                throw;
            }
            warnings_.push_back("skipped " + path.string() + ": " + e.what());
            continue;
        }
        archive_names_.push_back(std::filesystem::relative(path, dir).generic_string());
        const size_t archive = archives_.size() - 1;
        const auto& entries = archives_.back()->entries();
        for (size_t entry = 0; entry < entries.size(); ++entry) {
            const std::string key = normalize_path(entries[entry].path);
            if (verified()) {
                // A candidate only if it's the size of the copy the reference loads.
                const auto ref = reference_.find(key);
                if (ref != reference_.end() && ref->second.size == entries[entry].size) {
                    index_[key].push_back({archive, entry});
                }
                continue;
            }
            auto& candidates = index_[key];
            if (candidates.empty()) {
                candidates.push_back({archive, entry});
            } else if (higher_priority(archive_names_[archive], archive_names_[candidates[0].archive])) {
                candidates[0] = {archive, entry};
            }
        }
    }
}

bool TreLibrary::contains(std::string_view path) const {
    return index_.contains(normalize_path(path));
}

std::vector<std::string> TreLibrary::paths() const {
    std::vector<std::string> out;
    out.reserve(index_.size());
    for (const auto& [path, location] : index_) {
        out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint8_t> TreLibrary::read(std::string_view path) {
    const std::string key = normalize_path(path);
    const auto it = index_.find(key);
    if (it == index_.end()) {
        throw std::runtime_error("not found in any archive: " + std::string(path));
    }
    for (const Location& location : it->second) {
        TreArchive& archive = *archives_[location.archive];
        std::vector<uint8_t> data = archive.read(archive.entries()[location.entry]);
        if (!verified() || Sha256::hex_of(data.data(), data.size()) == reference_.at(key).sha256) {
            return data;
        }
    }
    throw std::runtime_error("no SOE original of " + std::string(path) +
                             ": every copy found differs from the reference manifest");
}

std::string TreLibrary::archive_of(std::string_view path) const {
    const auto it = index_.find(normalize_path(path));
    return it == index_.end() ? std::string() : archive_names_[it->second.front().archive];
}

} // namespace swbfo::swg
