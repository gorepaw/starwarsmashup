#include "swg/manifest.h"

#include <algorithm>
#include <cctype>
#include <istream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <tuple>

#include "swg/sha256.h"
#include "swg/tre_scan.h"

namespace swm::swg {
namespace {

constexpr std::string_view format_line = "# swm-manifest 1";
constexpr std::string_view name_prefix = "# name: ";
constexpr std::string_view source_prefix = "# source: ";

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
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
    const std::vector<std::filesystem::path> archives = find_tre_archives(dir);

    std::vector<std::vector<ManifestEntry>> archive_entries(archives.size());
    TreVisitResult visit = visit_tre_archives(
        archives, dir, log,
        [&](size_t archive_index, const std::string& archive_name, const TreEntry& entry,
            const std::vector<uint8_t>& data) {
            archive_entries[archive_index].push_back(
                {Sha256::hex_of(data.data(), data.size()), data.size(), archive_name, normalize_path(entry.path)});
        });

    ScanResult result;
    result.archive_count = archives.size();
    result.errors = std::move(visit.errors);
    for (auto& entries : archive_entries) {
        std::move(entries.begin(), entries.end(), std::back_inserter(result.entries));
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
