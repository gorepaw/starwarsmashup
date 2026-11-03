// swgtool: inspect SWG TRE archives, build reference manifests, and measure how
// much of a reference an install contains.

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "swg/manifest.h"
#include "swg/tre_archive.h"

namespace {

using namespace swm::swg;

constexpr const char* usage = R"(usage:
  swgtool list <archive.tre>
  swgtool manifest <dir> --name <name> --source <text> -o <out.tsv>
  swgtool coverage <manifest.tsv> <dir> [--missing <out.tsv>]
)";

// Returns the value following `flag` in args, or an empty string.
std::string option(const std::vector<std::string>& args, const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    return it != args.end() && it + 1 != args.end() ? *(it + 1) : std::string();
}

std::string megabytes(uint64_t bytes) {
    return std::format("{:.1f} MB", double(bytes) / (1024.0 * 1024.0));
}

std::string percent(uint64_t part, uint64_t whole) {
    return whole == 0 ? "-" : std::format("{:.1f}%", 100.0 * double(part) / double(whole));
}

void print_errors(const std::vector<std::string>& errors) {
    if (errors.empty()) {
        return;
    }
    std::cerr << errors.size() << " errors:\n";
    for (size_t i = 0; i < errors.size() && i < 20; ++i) {
        std::cerr << "  " << errors[i] << "\n";
    }
    if (errors.size() > 20) {
        std::cerr << "  ...\n";
    }
}

int cmd_list(const std::vector<std::string>& args) {
    if (args.size() < 1) {
        std::cerr << usage;
        return 2;
    }
    TreArchive tre(args[0]);
    std::cout << std::format("TRE version {:04}, {} files\n", tre.version(), tre.entries().size());
    for (const TreEntry& entry : tre.entries()) {
        std::cout << std::format("{:>10} {:>10} {} {}\n", entry.size, entry.compressed_size,
                                 entry.compressor == tre_compressor_zlib ? "z" : "-", entry.path);
    }
    return 0;
}

int cmd_manifest(const std::vector<std::string>& args) {
    const std::string name = option(args, "--name");
    const std::string source = option(args, "--source");
    const std::string out_path = option(args, "-o");
    if (args.empty() || name.empty() || source.empty() || out_path.empty()) {
        std::cerr << usage;
        return 2;
    }

    ScanResult scan = scan_tre_directory(args[0], std::cerr);
    print_errors(scan.errors);
    if (!scan.errors.empty()) {
        std::cerr << "refusing to write a manifest from a scan with errors\n";
        return 1;
    }

    Manifest manifest{name, source, std::move(scan.entries)};
    std::ofstream out(out_path, std::ios::binary);
    write_manifest(manifest, out);
    if (!out) {
        std::cerr << "failed to write " << out_path << "\n";
        return 1;
    }
    std::cerr << std::format("wrote {} entries from {} archives to {}\n", manifest.entries.size(),
                             scan.archive_count, out_path);
    return 0;
}

// For every unique path in the reference, decide how the install relates to it.
enum class Status { matched, moved, changed, absent };

const char* status_name(Status status) {
    switch (status) {
    case Status::matched: return "matched";
    case Status::moved: return "moved";
    case Status::changed: return "changed";
    case Status::absent: return "absent";
    }
    return "?";
}

struct Tally {
    uint64_t count[4] = {};
    uint64_t bytes[4] = {};
    uint64_t total_count = 0;
    uint64_t total_bytes = 0;

    void add(Status status, uint64_t size) {
        count[int(status)] += 1;
        bytes[int(status)] += size;
        total_count += 1;
        total_bytes += size;
    }
};

int cmd_coverage(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    std::ifstream manifest_file(args[0], std::ios::binary);
    if (!manifest_file) {
        std::cerr << "cannot open " << args[0] << "\n";
        return 1;
    }
    const Manifest reference = read_manifest(manifest_file);

    // Reference: path -> every SOE version of it (hash, size).
    std::map<std::string, std::vector<const ManifestEntry*>> reference_by_path;
    std::unordered_set<std::string> reference_hashes;
    for (const ManifestEntry& entry : reference.entries) {
        reference_by_path[entry.path].push_back(&entry);
        reference_hashes.insert(entry.sha256);
    }

    const ScanResult install = scan_tre_directory(args[1], std::cerr);
    print_errors(install.errors);

    std::unordered_map<std::string, std::unordered_set<std::string>> install_by_path;
    std::unordered_set<std::string> install_hashes;
    uint64_t other_count = 0;
    uint64_t other_bytes = 0;
    for (const ManifestEntry& entry : install.entries) {
        install_by_path[entry.path].insert(entry.sha256);
        install_hashes.insert(entry.sha256);
        if (!reference_hashes.contains(entry.sha256)) {
            ++other_count;
            other_bytes += entry.size;
        }
    }

    Tally total;
    std::map<std::string, Tally> by_folder;
    std::vector<std::pair<Status, std::string>> not_matched;

    for (const auto& [path, versions] : reference_by_path) {
        const auto installed = install_by_path.find(path);
        const auto in_install_at_path = [&](const ManifestEntry* v) {
            return installed != install_by_path.end() && installed->second.contains(v->sha256);
        };
        const auto in_install_anywhere = [&](const ManifestEntry* v) { return install_hashes.contains(v->sha256); };

        Status status = Status::absent;
        if (std::any_of(versions.begin(), versions.end(), in_install_at_path)) {
            status = Status::matched;
        } else if (std::any_of(versions.begin(), versions.end(), in_install_anywhere)) {
            status = Status::moved;
        } else if (installed != install_by_path.end()) {
            status = Status::changed;
        }

        uint64_t size = 0;
        for (const ManifestEntry* v : versions) {
            size = std::max(size, v->size);
        }
        const std::string folder = path.substr(0, path.find('/'));
        total.add(status, size);
        by_folder[folder].add(status, size);
        if (status != Status::matched) {
            not_matched.emplace_back(status, path);
        }
    }

    std::cout << std::format("Reference: {} ({} unique files, {})\n", reference.name, total.total_count,
                             megabytes(total.total_bytes));
    std::cout << std::format("Install:   {} ({} archives, {} files)\n\n", args[1], install.archive_count,
                             install.entries.size());

    std::cout << "Reference files, by what the install has:\n";
    const char* descriptions[4] = {
        "byte-identical at the same path",
        "byte-identical, but only under another path",
        "path exists, contents differ",
        "not present",
    };
    for (int s = 0; s < 4; ++s) {
        std::cout << std::format("  {:<8} {:>7} files {:>7}  {:>12} {:>7}  {}\n", status_name(Status(s)),
                                 total.count[s], percent(total.count[s], total.total_count),
                                 megabytes(total.bytes[s]), percent(total.bytes[s], total.total_bytes),
                                 descriptions[s]);
    }

    std::cout << "\nBy top-level folder (matched / total files):\n";
    for (const auto& [folder, tally] : by_folder) {
        const uint64_t matched = tally.count[int(Status::matched)];
        std::cout << std::format("  {:<24} {:>7} / {:<7} {:>7}\n", folder, matched, tally.total_count,
                                 percent(matched, tally.total_count));
    }

    std::cout << std::format("\nInstall files not in the reference (other work or later patches): {} files, {}\n",
                             other_count, megabytes(other_bytes));

    const std::string missing_path = option(args, "--missing");
    if (!missing_path.empty()) {
        std::ofstream missing(missing_path, std::ios::binary);
        for (const auto& [status, path] : not_matched) {
            missing << status_name(status) << '\t' << path << '\n';
        }
        std::cout << std::format("Wrote {} unmatched paths to {}\n", not_matched.size(), missing_path);
    }
    return install.errors.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    try {
        if (command == "list") {
            return cmd_list(args);
        }
        if (command == "manifest") {
            return cmd_manifest(args);
        }
        if (command == "coverage") {
            return cmd_coverage(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
