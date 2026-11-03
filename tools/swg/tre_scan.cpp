#include "swg/tre_scan.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iterator>
#include <mutex>
#include <ostream>
#include <thread>

namespace swm::swg {

std::vector<std::filesystem::path> find_tre_archives(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> archives;
    for (const auto& item : std::filesystem::recursive_directory_iterator(dir)) {
        std::string extension = item.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (item.is_regular_file() && extension == ".tre") {
            archives.push_back(item.path());
        }
    }
    std::sort(archives.begin(), archives.end());
    return archives;
}

TreVisitResult visit_tre_archives(const std::vector<std::filesystem::path>& archives,
                                  const std::filesystem::path& base_dir, std::ostream& log, const TreVisitor& visitor) {
    std::vector<std::vector<std::string>> archive_errors(archives.size());
    std::atomic<size_t> next_archive{0};
    std::mutex log_mutex;
    size_t finished = 0;

    auto worker = [&] {
        for (size_t i; (i = next_archive++) < archives.size();) {
            const std::string archive_name = std::filesystem::relative(archives[i], base_dir).generic_string();
            size_t visited = 0;
            try {
                TreArchive tre(archives[i]);
                // Read in file order to keep disk access sequential.
                std::vector<TreEntry> entries = tre.entries();
                std::sort(entries.begin(), entries.end(),
                          [](const TreEntry& a, const TreEntry& b) { return a.offset < b.offset; });
                for (const TreEntry& entry : entries) {
                    try {
                        visitor(i, archive_name, entry, tre.read(entry));
                        ++visited;
                    } catch (const std::exception& e) {
                        archive_errors[i].push_back(std::string(e.what()) + " [" + archive_name + ": " +
                                                    entry.path + "]");
                    }
                }
            } catch (const std::exception& e) {
                archive_errors[i].push_back(e.what());
            }

            std::lock_guard lock(log_mutex);
            log << "[" << ++finished << "/" << archives.size() << "] " << archive_name << ": " << visited
                << " files";
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

    TreVisitResult result;
    for (auto& errors : archive_errors) {
        std::move(errors.begin(), errors.end(), std::back_inserter(result.errors));
    }
    return result;
}

} // namespace swm::swg
