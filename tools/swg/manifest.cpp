#include "swg/manifest.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <istream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <tuple>

#include "swg/sha256.h"
#include "swg/tre_archive.h"

namespace swm::swg {
namespace {

constexpr std::string_view format_line = "# swm-manifest 1";
constexpr std::string_view name_prefix = "# name: ";
constexpr std::string_view source_prefix = "# source: ";

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::vector<std::filesystem::path> find_archives(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> archives;
    for (const auto& item : std::filesystem::recursive_directory_iterator(dir)) {
        if (item.is_regular_file() && to_lower(item.path().extension().string()) == ".tre") {
            archives.push_back(item.path());
        }
    }
    std::sort(archives.begin(), archives.end());
    return archives;
}

bool entry_less(const ManifestEntry& a, const ManifestEntry& b) {
    return std::tie(a.path, a.archive) < std::tie(b.path, b.archive);
}

} // namespace

std::string normalize_path(std::string_view path) {
    std::string out = to_lower(std::string(path));
    std::replace(out.begin(), out.end(), '\\', '/');
    const size_t start = out.find_first_not_of('/');
    return start == std::string::npos ? std::string() : out.substr(start);
}

ScanResult scan_tre_directory(const std::filesystem::path& dir, std::ostream& log) {
    const std::vector<std::filesystem::path> archives = find_archives(dir);

    std::vector<std::vector<ManifestEntry>> archive_entries(archives.size());
    std::vector<std::vector<std::string>> archive_errors(archives.size());
    std::atomic<size_t> next_archive{0};
    std::mutex log_mutex;
    size_t finished = 0;

    auto worker = [&] {
        for (size_t i; (i = next_archive++) < archives.size();) {
            const std::string archive_name = std::filesystem::relative(archives[i], dir).generic_string();
            try {
                TreArchive tre(archives[i]);
                // Read in file order to keep disk access sequential.
                std::vector<TreEntry> entries = tre.entries();
                std::sort(entries.begin(), entries.end(),
                          [](const TreEntry& a, const TreEntry& b) { return a.offset < b.offset; });
                for (const TreEntry& entry : entries) {
                    try {
                        const std::vector<uint8_t> data = tre.read(entry);
                        archive_entries[i].push_back({Sha256::hex_of(data.data(), data.size()), data.size(),
                                                      archive_name, normalize_path(entry.path)});
                    } catch (const std::exception& e) {
                        archive_errors[i].push_back(std::string(e.what()) + " [" + entry.path + "]");
                    }
                }
            } catch (const std::exception& e) {
                archive_errors[i].push_back(e.what());
            }

            std::lock_guard lock(log_mutex);
            log << "[" << ++finished << "/" << archives.size() << "] " << archive_name << ": "
                << archive_entries[i].size() << " files";
            if (!archive_errors[i].empty()) {
                log << ", " << archive_errors[i].size() << " errors";
            }
            log << "\n";
        }
    };

    const size_t thread_count =
        std::clamp<size_t>(std::thread::hardware_concurrency(), 1, std::max<size_t>(archives.size(), 1));
    {
        std::vector<std::jthread> pool;
        for (size_t t = 0; t < thread_count; ++t) {
            pool.emplace_back(worker);
        }
    }

    ScanResult result;
    result.archive_count = archives.size();
    for (size_t i = 0; i < archives.size(); ++i) {
        std::move(archive_entries[i].begin(), archive_entries[i].end(), std::back_inserter(result.entries));
        std::move(archive_errors[i].begin(), archive_errors[i].end(), std::back_inserter(result.errors));
    }
    std::sort(result.entries.begin(), result.entries.end(), entry_less);
    return result;
}

void write_manifest(const Manifest& manifest, std::ostream& out) {
    out << format_line << "\n";
    out << name_prefix << manifest.name << "\n";
    out << source_prefix << manifest.source << "\n";
    out << "# hash: sha256 of uncompressed file contents\n";
    out << "# columns: sha256 size archive path\n";
    for (const ManifestEntry& entry : manifest.entries) {
        out << entry.sha256 << '\t' << entry.size << '\t' << entry.archive << '\t' << entry.path << '\n';
    }
}

Manifest read_manifest(std::istream& in) {
    Manifest manifest;
    std::string line;
    size_t line_number = 0;
    bool saw_format = false;

    while (std::getline(in, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            if (line == format_line) {
                saw_format = true;
            } else if (line.starts_with(name_prefix)) {
                manifest.name = line.substr(name_prefix.size());
            } else if (line.starts_with(source_prefix)) {
                manifest.source = line.substr(source_prefix.size());
            }
            continue;
        }

        const size_t tab1 = line.find('\t');
        const size_t tab2 = tab1 == std::string::npos ? tab1 : line.find('\t', tab1 + 1);
        const size_t tab3 = tab2 == std::string::npos ? tab2 : line.find('\t', tab2 + 1);
        if (tab3 == std::string::npos) {
            throw std::runtime_error("manifest line " + std::to_string(line_number) + ": expected 4 columns");
        }
        ManifestEntry entry;
        entry.sha256 = line.substr(0, tab1);
        entry.size = std::stoull(line.substr(tab1 + 1, tab2 - tab1 - 1));
        entry.archive = line.substr(tab2 + 1, tab3 - tab2 - 1);
        entry.path = line.substr(tab3 + 1);
        manifest.entries.push_back(std::move(entry));
    }

    if (!saw_format) {
        throw std::runtime_error("not a swm manifest (missing '" + std::string(format_line) + "')");
    }
    return manifest;
}

} // namespace swm::swg
