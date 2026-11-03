// bf2tool: inspect Battlefront II munged files (.lvl).

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "bf2/animation.h"
#include "bf2/hash.h"
#include "bf2/ucfb.h"

namespace {

using namespace swm;
using namespace swm::bf2;

constexpr const char* usage = R"(usage:
  bf2tool tree <file.lvl> [--depth <n>]
  bf2tool list <file.lvl> <tag>
  bf2tool check <dir>
  bf2tool skeleton <file.lvl> <bank>
  bf2tool anims <file.lvl> <bank> [name...]
)";

std::vector<uint8_t> read_file(const std::filesystem::path& path);

// BIN_ payload of the chunk with `tag` and NAME `bank`.
std::span<const uint8_t> bank_payload(const UcfbNode& root, std::string_view tag, const std::string& bank) {
    const UcfbNode* chunk = find_named_chunk(root, tag, bank);
    const UcfbNode* bin = chunk ? chunk->child("BIN_") : nullptr;
    if (!bin) {
        throw std::runtime_error("no " + std::string(tag) + " chunk named " + bank);
    }
    return bin->data;
}

int cmd_skeleton(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::vector<uint8_t> bytes = read_file(args[0]);
    const AnimationSkeleton skeleton = parse_animation_skeleton(bank_payload(parse_ucfb(bytes), "zaf_", args[1]));
    std::vector<Quat> world_rotation;
    std::vector<Vec3> world_position;
    for (const AnimationJoint& joint : skeleton.joints) {
        const int parent = joint.parent_hash == 0 ? -1 : skeleton.find(joint.parent_hash);
        const Quat parent_rotation = parent < 0 ? Quat{} : world_rotation[size_t(parent)];
        const Vec3 parent_position = parent < 0 ? Vec3{} : world_position[size_t(parent)];
        world_rotation.push_back(parent_rotation * joint.rotation);
        world_position.push_back(parent_position + rotate(parent_rotation, joint.translation));
        const Vec3 p = world_position.back();
        std::cout << std::format("{:18} parent {:18} rest world position ({:7.3f}, {:7.3f}, {:7.3f})\n",
                                 bone_label(joint.hash), joint.parent_hash ? bone_label(joint.parent_hash) : "-",
                                 p.x, p.y, p.z);
    }
    return 0;
}

int cmd_anims(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::vector<uint8_t> bytes = read_file(args[0]);
    const std::vector<Animation> animations = parse_animation_bank(bank_payload(parse_ucfb(bytes), "zaa_", args[1]));
    std::map<uint32_t, std::string> names;
    for (size_t i = 2; i < args.size(); ++i) {
        names[name_hash(args[i])] = args[i];
    }
    for (const Animation& animation : animations) {
        const auto name = names.find(animation.name_hash);
        std::cout << std::format("{:08x} {:>4} frames {:>3} bones  {}\n", animation.name_hash, animation.frame_count,
                                 animation.tracks.size(), name == names.end() ? "" : name->second);
    }
    return 0;
}

std::string option(const std::vector<std::string>& args, const std::string& flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    return it != args.end() && it + 1 != args.end() ? *(it + 1) : std::string();
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(path.string() + ": cannot open");
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

int cmd_tree(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::string depth_option = option(args, "--depth");
    const int max_depth = depth_option.empty() ? 3 : std::stoi(depth_option);
    const std::vector<uint8_t> bytes = read_file(args[0]);
    visit_ucfb(parse_ucfb(bytes), [&](const UcfbNode& node, int depth) {
        if (depth > max_depth) {
            return;
        }
        std::string line = std::format("{}{} {}", std::string(size_t(depth) * 2, ' '), node.tag, node.data.size());
        if (node.tag == "lvl_") {
            line += std::format(" hash={:08x}", node.name_hash);
        }
        if (const std::string name = node.name(); !name.empty()) {
            line += "  \"" + name + "\"";
        }
        std::cout << line << "\n";
    });
    return 0;
}

int cmd_list(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::vector<uint8_t> bytes = read_file(args[0]);
    size_t count = 0;
    visit_ucfb(parse_ucfb(bytes), [&](const UcfbNode& node, int) {
        if (node.tag == args[1]) {
            std::cout << std::format("{:>10}  {}\n", node.data.size(), node.name());
            ++count;
        }
    });
    std::cerr << count << " '" << args[1] << "' chunks\n";
    return 0;
}

int cmd_check(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(args[0])) {
        if (item.is_regular_file() && item.path().extension() == ".lvl") {
            files.push_back(item.path());
        }
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, uint64_t> content_tags;  // chunks directly inside lvl_ sublevels or the root
    std::vector<std::string> failures;
    uint64_t total_bytes = 0;
    for (const auto& path : files) {
        try {
            const std::vector<uint8_t> bytes = read_file(path);
            total_bytes += bytes.size();
            const UcfbNode root = parse_ucfb(bytes);
            visit_ucfb(root, [&](const UcfbNode& node, int depth) {
                if (depth == 0 || node.tag == "lvl_") {
                    for (const UcfbNode& child : node.children) {
                        if (child.tag != "lvl_") {
                            content_tags[child.tag] += 1;
                        }
                    }
                }
            });
        } catch (const std::exception& e) {
            failures.push_back(std::filesystem::relative(path, args[0]).string() + ": " + e.what());
        }
    }

    std::vector<std::pair<std::string, uint64_t>> rows(content_tags.begin(), content_tags.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::cout << std::format("{} .lvl files, {:.1f} MB, {} failed\n\nContent chunks:\n", files.size(),
                             double(total_bytes) / (1024.0 * 1024.0), failures.size());
    for (const auto& [tag, count] : rows) {
        std::cout << std::format("  {}  {:>7}\n", tag, count);
    }
    for (const std::string& failure : failures) {
        std::cout << "  FAILED " << failure << "\n";
    }
    return failures.empty() ? 0 : 1;
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
        if (command == "tree") {
            return cmd_tree(args);
        }
        if (command == "list") {
            return cmd_list(args);
        }
        if (command == "check") {
            return cmd_check(args);
        }
        if (command == "skeleton") {
            return cmd_skeleton(args);
        }
        if (command == "anims") {
            return cmd_anims(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
