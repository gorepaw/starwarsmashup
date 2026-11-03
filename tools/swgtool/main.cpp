// swgtool: inspect SWG TRE archives, build reference manifests, and measure how
// much of a reference an install contains.

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bf2/animation.h"
#include "bf2/hash.h"
#include "bf2/ucfb.h"
#include "retarget/retarget.h"
#include "swg/character_export.h"
#include "swg/iff.h"
#include "swg/image.h"
#include "swg/terrain.h"
#include "swg/world_snapshot.h"
#include "swg/manifest.h"
#include "swg/tre_archive.h"
#include "swg/tre_scan.h"

namespace {

using namespace swm;
using namespace swm::swg;

constexpr const char* usage = R"(usage:
  swgtool list <archive.tre>
  swgtool manifest <dir> --name <name> --source <text> -o <out.tsv>
  swgtool coverage <manifest.tsv> <dir> [--missing <out.tsv>]
  swgtool groups <manifest.tsv> [--list <group>]
  swgtool iff <archive.tre> <path>
  swgtool iff-check <dir>
  swgtool export-character <dir> <out_dir> --name <name> --sat <path.sat>... [--weapon <path.lod>]
                           [--bf2-lvl <ingame.lvl> --bf2-bank <bank> --anim <name>...]
  swgtool bake-terrain <dir> <out_prefix> --trn <path.trn> --center <x> <z> --samples <n> --spacing <m>
                       [--snapshot <path.ws>]
)";

// Returns the value following `flag` in args, or an empty string.
std::string option(const std::vector<std::string>& args, const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    return it != args.end() && it + 1 != args.end() ? *(it + 1) : std::string();
}

// Every value following `flag` in args (the flag may repeat).
std::vector<std::string> options(const std::vector<std::string>& args, const std::string& flag) {
    std::vector<std::string> values;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) {
            values.push_back(args[i + 1]);
        }
    }
    return values;
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

    const std::map<std::string, std::string> groups = path_content_groups(reference);
    Tally total;
    std::map<std::string, Tally> by_folder;
    std::map<std::string, Tally> by_group;
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
        by_group[groups.at(path)].add(status, size);
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

    std::cout << "\nBy content group (matched / total files):\n";
    for (const auto& [group, tally] : by_group) {
        const uint64_t matched = tally.count[int(Status::matched)];
        std::cout << std::format("  {:<24} {:>7} / {:<7} {:>7}\n", group, matched, tally.total_count,
                                 percent(matched, tally.total_count));
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

int cmd_groups(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    std::ifstream manifest_file(args[0], std::ios::binary);
    if (!manifest_file) {
        std::cerr << "cannot open " << args[0] << "\n";
        return 1;
    }
    const Manifest manifest = read_manifest(manifest_file);
    const std::map<std::string, std::string> groups = path_content_groups(manifest);

    const std::string list_group = option(args, "--list");
    if (!list_group.empty()) {
        for (const auto& [path, group] : groups) {
            if (group == list_group) {
                std::cout << path << "\n";
            }
        }
        return 0;
    }

    std::map<std::string, uint64_t> largest_size;
    for (const ManifestEntry& entry : manifest.entries) {
        largest_size[entry.path] = std::max(largest_size[entry.path], entry.size);
    }
    std::map<std::string, std::pair<uint64_t, uint64_t>> totals;  // group -> (files, bytes)
    for (const auto& [path, group] : groups) {
        totals[group].first += 1;
        totals[group].second += largest_size[path];
    }

    std::cout << std::format("{} ({} unique files)\n", manifest.name, groups.size());
    for (const auto& [group, total] : totals) {
        std::cout << std::format("  {:<10} {:>7} files {:>7}  {:>12}\n", group, total.first,
                                 percent(total.first, groups.size()), megabytes(total.second));
    }
    return 0;
}

std::string extension_of(const std::string& path) {
    const size_t slash = path.rfind('/');
    const size_t dot = path.rfind('.');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "(none)" : path.substr(dot);
}

void print_iff_tree(const std::vector<IffNode>& nodes, int depth) {
    for (const IffNode& node : nodes) {
        const std::string indent(size_t(depth) * 2, ' ');
        if (node.form) {
            std::cout << indent << "FORM " << node.tag << "\n";
            print_iff_tree(node.children, depth + 1);
            continue;
        }
        std::string preview;
        for (size_t i = 0; i < node.data.size() && i < 16; ++i) {
            preview += std::format("{:02x} ", node.data[i]);
        }
        std::cout << std::format("{}{} {:>8}  {}{}\n", indent, node.tag, node.data.size(), preview,
                                 node.data.size() > 16 ? "..." : "");
    }
}

int cmd_iff(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    TreArchive tre(args[0]);
    const std::string wanted = normalize_path(args[1]);
    for (const TreEntry& entry : tre.entries()) {
        if (normalize_path(entry.path) == wanted) {
            const std::vector<uint8_t> data = tre.read(entry);
            print_iff_tree(parse_iff(data), 0);
            return 0;
        }
    }
    std::cerr << "not found in archive: " << args[1] << "\n";
    return 1;
}

struct ExtensionStats {
    uint64_t files = 0;
    uint64_t iff = 0;
    uint64_t failed = 0;
    uint64_t multiple_roots = 0;
    std::map<std::string, uint64_t> root_forms;
};

int cmd_iff_check(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    std::mutex mutex;
    std::map<std::string, ExtensionStats> by_extension;
    std::vector<std::string> parse_failures;

    const auto archives = find_tre_archives(args[0]);
    const TreVisitResult visit = visit_tre_archives(
        archives, args[0], std::cerr,
        [&](size_t, const std::string& archive_name, const TreEntry& entry, const std::vector<uint8_t>& data) {
            const std::string path = normalize_path(entry.path);
            const bool iff = looks_like_iff(data);
            std::string failure;
            std::vector<IffNode> roots;
            if (iff) {
                try {
                    roots = parse_iff(data);
                } catch (const std::exception& e) {
                    failure = std::string(e.what()) + " [" + archive_name + ": " + path + "]";
                }
            }

            std::lock_guard lock(mutex);
            ExtensionStats& stats = by_extension[extension_of(path)];
            stats.files += 1;
            if (!iff) {
                return;
            }
            stats.iff += 1;
            if (!failure.empty()) {
                stats.failed += 1;
                parse_failures.push_back(std::move(failure));
                return;
            }
            stats.multiple_roots += roots.size() > 1 ? 1 : 0;
            stats.root_forms[roots.front().tag] += 1;
        });
    print_errors(visit.errors);

    std::vector<std::pair<std::string, ExtensionStats>> rows(by_extension.begin(), by_extension.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.files > b.second.files; });

    uint64_t total_iff = 0;
    uint64_t total_failed = 0;
    std::cout << std::format("{:<10} {:>8} {:>8} {:>7} {:>6}  {}\n", "extension", "files", "iff", "failed", "multi",
                             "root forms");
    for (const auto& [extension, stats] : rows) {
        std::string forms;
        for (const auto& [form_name, count] : stats.root_forms) {
            forms += std::format("{}({}) ", form_name, count);
            if (forms.size() > 60) {
                forms += "...";
                break;
            }
        }
        std::cout << std::format("{:<10} {:>8} {:>8} {:>7} {:>6}  {}\n", extension, stats.files, stats.iff,
                                 stats.failed, stats.multiple_roots, forms);
        total_iff += stats.iff;
        total_failed += stats.failed;
    }
    std::cout << std::format("\n{} IFF files parsed, {} failed\n", total_iff, total_failed);
    for (size_t i = 0; i < parse_failures.size() && i < 20; ++i) {
        std::cout << "  " << parse_failures[i] << "\n";
    }
    return total_failed == 0 && visit.errors.empty() ? 0 : 1;
}

int cmd_export_character(const std::vector<std::string>& args) {
    CharacterExport request;
    request.name = option(args, "--name");
    request.appearances = options(args, "--sat");
    request.weapon_lod = option(args, "--weapon");
    if (args.size() < 2 || request.name.empty() || request.appearances.empty()) {
        std::cerr << usage;
        return 2;
    }
    TreLibrary library(args[0]);

    const std::string bf2_lvl = option(args, "--bf2-lvl");
    const std::vector<std::string> anim_names = options(args, "--anim");
    if (!bf2_lvl.empty() && !anim_names.empty()) {
        const std::string bank = option(args, "--bf2-bank");
        std::ifstream lvl_file(bf2_lvl, std::ios::binary);
        if (!lvl_file || bank.empty()) {
            std::cerr << "cannot open " << bf2_lvl << " (or no --bf2-bank)\n";
            return 1;
        }
        const std::vector<uint8_t> lvl(std::istreambuf_iterator<char>(lvl_file), {});
        const bf2::UcfbNode root = bf2::parse_ucfb(lvl);
        const bf2::UcfbNode* zaf = bf2::find_named_chunk(root, "zaf_", bank);
        const bf2::UcfbNode* zaa = bf2::find_named_chunk(root, "zaa_", bank);
        if (!zaf || !zaa || !zaf->child("BIN_") || !zaa->child("BIN_")) {
            std::cerr << "no animation bank '" << bank << "' in " << bf2_lvl << "\n";
            return 1;
        }
        const bf2::AnimationSkeleton bf2_skeleton = bf2::parse_animation_skeleton(zaf->child("BIN_")->data);
        const std::vector<bf2::Animation> bank_animations = bf2::parse_animation_bank(zaa->child("BIN_")->data);
        const Skeleton skeleton = load_appearance_skeleton(library, request.appearances.front());
        for (const std::string& anim_name : anim_names) {
            const auto it = std::find_if(bank_animations.begin(), bank_animations.end(),
                                         [&](const bf2::Animation& a) { return a.name_hash == bf2::name_hash(anim_name); });
            if (it == bank_animations.end()) {
                std::cerr << "no animation '" << anim_name << "' in bank " << bank << "\n";
                return 1;
            }
            request.clips.push_back(
                retarget::retarget_animation(skeleton, bf2_skeleton, *it, retarget::humanoid_mapping(), anim_name));
        }
    }

    export_character(library, request, args[1], std::cerr);
    std::cerr << "wrote " << (std::filesystem::path(args[1]) / (request.name + ".gltf")).string() << "\n";
    return 0;
}

// Shaded relief with a sand-to-rock tint, for eyeballing a bake.
ImageRgba hillshade(const HeightGrid& grid) {
    const auto [lo_it, hi_it] = std::minmax_element(grid.heights.begin(), grid.heights.end());
    const float lo = *lo_it, range = std::max(*hi_it - *lo_it, 1.0f);
    const Vec3 light = normalized(Vec3{-1, 2, -1});
    ImageRgba image{uint32_t(grid.samples), uint32_t(grid.samples), {}};
    image.pixels.resize(size_t(grid.samples) * size_t(grid.samples) * 4);
    for (int row = 0; row < grid.samples; ++row) {
        for (int column = 0; column < grid.samples; ++column) {
            const int c0 = std::max(column - 1, 0), c1 = std::min(column + 1, grid.samples - 1);
            const int r0 = std::max(row - 1, 0), r1 = std::min(row + 1, grid.samples - 1);
            const float dx = (grid.at(c1, row) - grid.at(c0, row)) / float((c1 - c0) * grid.spacing);
            const float dz = (grid.at(column, r1) - grid.at(column, r0)) / float((r1 - r0) * grid.spacing);
            const float shade = std::clamp(dot(normalized(Vec3{-dx, 1, -dz}), light), 0.15f, 1.0f);
            const float t = (grid.at(column, row) - lo) / range;
            const float base[3] = {0.85f - 0.3f * t, 0.72f - 0.3f * t, 0.5f - 0.15f * t};
            // Image rows run top to bottom; put +Z (north) at the top.
            uint8_t* p = &image.pixels[(size_t(grid.samples - 1 - row) * size_t(grid.samples) + size_t(column)) * 4];
            for (int ch = 0; ch < 3; ++ch) {
                p[ch] = uint8_t(std::clamp(base[ch] * shade * 255.0f, 0.0f, 255.0f));
            }
            p[3] = 255;
        }
    }
    return image;
}

int cmd_bake_terrain(const std::vector<std::string>& args) {
    const std::string trn = option(args, "--trn");
    const auto center = std::find(args.begin(), args.end(), "--center");
    const std::string samples = option(args, "--samples"), spacing = option(args, "--spacing");
    if (args.size() < 2 || trn.empty() || center == args.end() || center + 2 >= args.end() || samples.empty() ||
        spacing.empty()) {
        std::cerr << usage;
        return 2;
    }
    const double cx = std::stod(*(center + 1)), cz = std::stod(*(center + 2));
    TreLibrary library(args[0]);
    const ProceduralTerrain terrain = parse_procedural_terrain(library.read(trn));
    std::cerr << std::format("{}: {} top-level layers, {} fractals\n", trn, terrain.layers.size(),
                             terrain.fractals.size());

    const HeightGrid grid = bake_heights(terrain, cx, cz, std::stoi(samples), std::stod(spacing));
    const auto [lo, hi] = std::minmax_element(grid.heights.begin(), grid.heights.end());

    const std::filesystem::path prefix = args[1];
    std::filesystem::create_directories(prefix.parent_path());
    std::ofstream raw(prefix.string() + ".r32", std::ios::binary);
    raw.write(reinterpret_cast<const char*>(grid.heights.data()), std::streamsize(grid.heights.size() * 4));
    std::ofstream meta(prefix.string() + ".json", std::ios::binary);
    meta << std::format(
        "{{\n  \"source\": \"{}\",\n  \"samples\": {},\n  \"spacing\": {},\n  \"origin_x\": {},\n  \"origin_z\": {},\n"
        "  \"min_height\": {},\n  \"max_height\": {},\n  \"format\": \"float32 little-endian, row-major, rows along +Z\"\n}}\n",
        trn, grid.samples, grid.spacing, grid.origin_x, grid.origin_z, *lo, *hi);
    write_png(prefix.string() + "_preview.png", hillshade(grid));
    std::cerr << std::format("heights {:.1f} .. {:.1f} m; wrote {}.r32/.json/_preview.png\n", *lo, *hi, prefix.string());

    const std::string snapshot_path = option(args, "--snapshot");
    if (!snapshot_path.empty()) {
        const double far_x = grid.origin_x + grid.spacing * (grid.samples - 1);
        const double far_z = grid.origin_z + grid.spacing * (grid.samples - 1);
        std::vector<double> errors;
        for (const SnapshotNode& node : parse_world_snapshot(library.read(snapshot_path))) {
            if (node.parent_id != 0 || node.position.x < grid.origin_x || node.position.x > far_x ||
                node.position.z < grid.origin_z || node.position.z > far_z) {
                continue;
            }
            errors.push_back(node.position.y - grid.sample_world(node.position.x, node.position.z));
        }
        if (errors.empty()) {
            std::cerr << "no snapshot objects in the region\n";
        } else {
            std::vector<double> magnitudes;
            for (const double e : errors) {
                magnitudes.push_back(std::abs(e));
            }
            std::sort(magnitudes.begin(), magnitudes.end());
            const auto within = [&](double limit) {
                return 100.0 * double(std::upper_bound(magnitudes.begin(), magnitudes.end(), limit) - magnitudes.begin()) /
                       double(magnitudes.size());
            };
            std::cerr << std::format(
                "snapshot check: {} world objects; |object y - terrain| median {:.2f} m, 90th pct {:.2f} m; "
                "within 1 m {:.0f}%, within 3 m {:.0f}%\n",
                magnitudes.size(), magnitudes[magnitudes.size() / 2], magnitudes[magnitudes.size() * 9 / 10], within(1.0),
                within(3.0));
        }
    }
    return 0;
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
        if (command == "groups") {
            return cmd_groups(args);
        }
        if (command == "iff") {
            return cmd_iff(args);
        }
        if (command == "iff-check") {
            return cmd_iff_check(args);
        }
        if (command == "export-character") {
            return cmd_export_character(args);
        }
        if (command == "bake-terrain") {
            return cmd_bake_terrain(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
