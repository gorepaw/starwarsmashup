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
#include "bf2/odf.h"
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
  bf2tool odf <file.lvl> [class]
  bf2tool export-soldier <_lvl_pc dir> <side> <soldier class> -o <out.ini>
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

// Lists a file's ODF classes, or prints one class's properties.
int cmd_odf(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::vector<uint8_t> bytes = read_file(args[0]);
    const std::vector<OdfClass> classes = read_odf_classes(parse_ucfb(bytes));
    for (const OdfClass& odf : classes) {
        if (args.size() < 2) {
            std::cout << std::format("{}  {:<40} base {:<32} {} properties\n", odf.kind, odf.type, odf.base,
                                     odf.properties.size());
            continue;
        }
        if (odf.type != args[1]) {
            continue;
        }
        std::cout << std::format("{} {} (base {})\n", odf.kind, odf.type, odf.base);
        for (const auto& [hash, value] : odf.properties) {
            const std::string_view name = odf_property_name(hash);
            std::cout << std::format("  {:<28} = {}\n", name.empty() ? std::format("{:08x}", hash) : std::string(name),
                                     value);
        }
    }
    return 0;
}

// A weapon a soldier carries, in its class's order: WeaponAmmo and
// WeaponChannel follow the WeaponName they belong to.
struct CarriedWeapon {
    std::string name;
    std::string ammo;
    int channel = 0;  // 0: primary, 1: secondary (grenades, mines, ...)
};

// A weapon's ordnance: its first OrdnanceName (a mortar's second is for a
// charged shot).
OdfClass ordnance_of(const OdfClass& weapon, const OdfLibrary& library) {
    for (const auto& [hash, value] : weapon.properties) {
        if (hash == odf_hash("OrdnanceName")) {
            return library.resolve(value);
        }
    }
    return library.resolve("");
}

// What a weapon fires, if the game supports it: "bolt" (a cannon firing
// bolts or bullets), "missile" (a launcher) or "grenade" (thrown or lobbed).
std::string projectile_of(const OdfClass& weapon, const OdfClass& ordnance) {
    if (weapon.base == "cannon" && (ordnance.base == "bolt" || ordnance.base == "bullet")) {
        return "bolt";
    }
    if (weapon.base == "launcher" && ordnance.base == "missile") {
        return "missile";
    }
    if (weapon.base == "grenade" && (ordnance.base == "sticky" || ordnance.base == "shell" || ordnance.base == "grenade")) {
        return "grenade";
    }
    return "";
}

// Writes one weapon section: the weapon, its ordnance and its explosion.
void write_weapon(std::ostream& out, const char* section, const CarriedWeapon& carried, const OdfLibrary& library,
                  const std::string& projectile) {
    const OdfClass weapon = library.resolve(carried.name);
    const OdfClass ordnance = ordnance_of(weapon, library);
    out << "\n[" << section << "]\nName = " << carried.name << "\nProjectile = " << projectile << "\n";
    if (!carried.ammo.empty()) {
        out << "WeaponAmmo = " << carried.ammo << "\n";
    }
    for (const char* key : {"ShotDelay", "RoundsPerClip", "ReloadTime", "HeatPerShot", "HeatRecoverRate",
                            "HeatThreshold", "YawSpread", "PitchSpread", "SpreadPerShot", "SpreadRecoverRate",
                            "SpreadLimit", "ShotsPerSalvo", "ShotElevate", "MaxRange"}) {
        if (const auto value = weapon.get(key)) {
            out << key << " = " << *value << "\n";
        }
    }
    out << "; ordnance: " << ordnance.type << " (" << ordnance.base << ")\n";
    for (const char* key : {"MaxDamage", "PersonScale", "Velocity", "Acceleration", "Gravity", "Rebound", "LifeSpan",
                            "LaserGlowColor", "LaserLength", "LaserWidth", "LightColor", "LightRadius"}) {
        if (const auto value = ordnance.get(key)) {
            out << key << " = " << *value << "\n";
        }
    }
    if (projectile == "grenade") {
        // A grenade's fuse: an unnamed ordnance property (hash a51be2bb: 2.0
        // on the thermal detonator, whose LifeSpan is 0.6), else LifeSpan.
        // ASSUMPTION: that property is the time from the throw to exploding.
        for (const auto& [hash, value] : ordnance.properties) {
            if (hash == 0xa51be2bbu) {
                out << "Fuse = " << value << "\n";
            }
        }
        out << "ExplosionImpact = " << (ordnance.get("ExplosionImpact") ? 1 : 0) << "\n";
    }
    std::string explosion_name = ordnance.get("ExplosionName").value_or("");
    for (const char* key : {"ExplosionImpact", "ExplosionExpire"}) {
        if (explosion_name.empty()) {
            explosion_name = ordnance.get(key).value_or("");
        }
    }
    if (explosion_name.empty()) {
        return;
    }
    const OdfClass explosion = library.resolve(explosion_name);
    out << "; explosion: " << explosion.type << "\n";
    if (const auto damage = explosion.get("Damage").value_or(ordnance.get("Damage").value_or("")); !damage.empty()) {
        out << "ExplosionDamage = " << damage << "\n";
    }
    for (const auto& [key, ours] : {std::pair{"DamageRadiusInner", "ExplosionRadiusInner"},
                                    std::pair{"DamageRadiusOuter", "ExplosionRadiusOuter"},
                                    std::pair{"PersonScale", "ExplosionPersonScale"}}) {
        if (const auto value = explosion.get(key)) {
            out << ours << " = " << *value << "\n";
        }
    }
}

// Writes a soldier class as a data file for core::load_soldier, inheritance
// resolved: its first two primary weapons the game supports, and its first
// grenade as the secondary.
int cmd_export_soldier(const std::vector<std::string>& args) {
    if (args.size() < 5 || args[3] != "-o") {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path dir = args[0];
    std::vector<std::vector<uint8_t>> files;  // the parsed trees view these bytes
    OdfLibrary library;
    for (const std::filesystem::path& path : {dir / "side" / (args[1] + ".lvl"), dir / "ingame.lvl"}) {
        files.push_back(read_file(path));
        library.add(parse_ucfb(files.back()));
    }
    const OdfClass soldier = library.resolve(args[2]);
    if (soldier.base != "soldier") {
        throw std::runtime_error(args[2] + " is not a soldier class");
    }

    std::ofstream out(args[4], std::ios::binary);
    out << "; " << args[2] << " from Battlefront II (" << args[1] << ".lvl, ingame.lvl), written by bf2tool export-soldier.\n";
    out << "; Game data from your own install: don't commit or share it.\n\n[soldier]\nName = " << args[2] << "\n";
    for (const char* key : {"MaxHealth", "MaxSpeed", "MaxStrafeSpeed", "JumpHeight", "Acceleration", "EnergyRestore",
                            "EnergyRestoreIdle", "EnergyDrainSprint", "EnergyMinSprint"}) {
        if (const auto value = soldier.get(key)) {
            out << key << " = " << *value << "\n";
        }
    }
    for (const auto& [hash, value] : soldier.properties) {
        if (hash == odf_hash("ControlSpeed")) {
            out << "ControlSpeed = " << value << "\n";
        }
    }

    // No award weapons: they're unlocks.
    std::vector<CarriedWeapon> carried;
    for (const auto& [hash, value] : soldier.properties) {
        if (hash == odf_hash("WeaponName")) {
            carried.push_back({value});
        } else if (!carried.empty() && hash == odf_hash("WeaponAmmo")) {
            carried.back().ammo = value;
        } else if (!carried.empty() && hash == odf_hash("WeaponChannel")) {
            carried.back().channel = std::atoi(value.c_str());
        }
    }
    int primaries = 0;
    bool secondary = false;
    for (const CarriedWeapon& weapon : carried) {
        if (weapon.name.find("award") != std::string::npos) {
            continue;
        }
        const OdfClass def = library.resolve(weapon.name);
        const OdfClass ordnance = ordnance_of(def, library);
        const std::string projectile = projectile_of(def, ordnance);
        const bool as_primary = weapon.channel == 0 && primaries < 2 && !projectile.empty();
        const bool as_secondary = weapon.channel == 1 && !secondary && projectile == "grenade";
        if (!as_primary && !as_secondary) {
            std::cerr << std::format("  skipped {} ({}, ordnance {})\n", weapon.name, def.base, ordnance.base);
            continue;
        }
        write_weapon(out, as_primary ? "weapon" : "secondary", weapon, library, projectile);
        if (as_primary) {
            ++primaries;
        } else {
            secondary = true;
        }
        std::cerr << std::format("  {} {} ({}, {})\n", as_primary ? "weapon" : "secondary", weapon.name, projectile,
                                 ordnance.type);
    }
    if (!out) {
        throw std::runtime_error("cannot write " + args[4]);
    }
    std::cerr << "wrote " << args[4] << "\n";
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
        if (command == "odf") {
            return cmd_odf(args);
        }
        if (command == "export-soldier") {
            return cmd_export_soldier(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
