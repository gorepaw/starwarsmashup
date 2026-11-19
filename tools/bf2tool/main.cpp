// bf2tool: inspect Battlefront II munged files (.lvl).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "bf2/animation.h"
#include "bf2/hash.h"
#include "bf2/level.h"
#include "bf2/model_export.h"
#include "bf2/odf.h"
#include "bf2/ucfb.h"
#include "swg/image.h"
#include "swm/core/world.h"

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
  bf2tool export-loadout <_lvl_pc dir> <side> <soldier class> -o <loadout.ini> [--items <dir>]
  bf2tool export-map <_lvl_pc dir> <map> -o <out dir> [--mode conquest] [--skip <layer>]...
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

// Writes a Battlefront soldier's kit as a test loadout (core::load_loadout):
// each weapon the game supports as an item file (core::load_weapon), and a
// loadout naming them: its first two primary weapons as the primary and the
// sidearm, and its first grenade as the secondary. Battlefront's per-class
// health and speed aren't kept: there are no classes in this game.
int cmd_export_loadout(const std::vector<std::string>& args) {
    if (args.size() < 5 || args[3] != "-o") {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path dir = args[0];
    const std::filesystem::path out_path = args[4];
    std::filesystem::path items = out_path.parent_path().parent_path() / "items";
    for (size_t i = 5; i + 1 < args.size(); i += 2) {
        if (args[i] == "--items") {
            items = args[i + 1];
        }
    }
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

    // The kit in order: WeaponAmmo and WeaponChannel follow their WeaponName.
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
    std::filesystem::create_directories(items);
    std::vector<std::string> held;  // primary, sidearm
    std::string secondary;
    for (const CarriedWeapon& weapon : carried) {
        if (weapon.name.find("award") != std::string::npos) {
            continue;
        }
        const OdfClass def = library.resolve(weapon.name);
        const OdfClass ordnance = ordnance_of(def, library);
        const std::string projectile = projectile_of(def, ordnance);
        const bool as_held = weapon.channel == 0 && held.size() < 2 && !projectile.empty();
        const bool as_secondary = weapon.channel == 1 && secondary.empty() && projectile == "grenade";
        if (!as_held && !as_secondary) {
            std::cerr << std::format("  skipped {} ({}, ordnance {})\n", weapon.name, def.base, ordnance.base);
            continue;
        }
        const std::filesystem::path item = items / (weapon.name + ".ini");
        std::ofstream file(item, std::ios::binary);
        file << "; " << weapon.name << " from Battlefront II (" << args[1]
             << ".lvl, ingame.lvl), written by bf2tool export-loadout.\n"
                "; Game data from your own install: don't commit or share it.\n";
        write_weapon(file, "weapon", weapon, library, projectile);
        if (!file) {
            throw std::runtime_error("cannot write " + item.string());
        }
        if (as_held) {
            held.push_back(weapon.name);
        } else {
            secondary = weapon.name;
        }
        std::cerr << std::format("  {} {} ({}, {})\n", as_held ? (held.size() == 1 ? "primary" : "sidearm") : "secondary",
                                 weapon.name, projectile, ordnance.type);
    }
    if (held.empty()) {
        throw std::runtime_error(args[2] + " carries no weapon the game supports");
    }

    std::filesystem::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary);
    out << "; A test loadout: " << args[2] << "'s kit from Battlefront II, written by bf2tool export-loadout.\n"
        << "; Its weapons are item files in " << items.generic_string() << ".\n\n[loadout]\nName = " << args[2] << "\n"
        << "Items = " << std::filesystem::relative(items, out_path.parent_path()).generic_string() << " ../../items\n"
        << "Primary = " << held[0] << "\n";
    if (held.size() > 1) {
        out << "Sidearm = " << held[1] << "\n";
    }
    if (!secondary.empty()) {
        out << "Secondary = " << secondary << "\n";
    }
    if (!out) {
        throw std::runtime_error("cannot write " + out_path.string());
    }
    std::cerr << "wrote " << out_path.string() << "\n";
    return 0;
}

std::string json_string(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += uint8_t(c) < 0x20 ? ' ' : c;
    }
    return out + "\"";
}

// Writes a Battlefront II map for the game: its terrain as a heightfield
// bake (<map>.r32/.json, like swgtool bake-terrain), its object placements in
// game space (<map>.objects.json), and a game mode's command posts as a map
// file for battlefields (<map>_<mode>.ini). Layers of other game modes, and
// any --skip layers (e.g. the other era's props), are left out.
int cmd_export_map(const std::vector<std::string>& args) {
    if (args.size() < 4 || args[2] != "-o") {
        std::cerr << usage;
        return 2;
    }
    const std::string map = args[1];
    const std::filesystem::path out_dir = args[3];
    std::string mode = "conquest";
    std::vector<std::string> skipped;
    for (size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--mode") {
            mode = args[i + 1];
        } else if (args[i] == "--skip") {
            skipped.push_back(args[i + 1]);
        }
    }
    const std::string folder = map.substr(0, 3);
    std::vector<uint8_t> bytes = read_file(std::filesystem::path(args[0]) / folder / (map + ".lvl"));
    const Level level = read_level(parse_ucfb(bytes));
    // Models and textures: the map's own, then the shared props' (ingame.lvl).
    AssetLibrary library;
    library.add(std::move(bytes));
    if (const std::filesystem::path ingame = std::filesystem::path(args[0]) / "ingame.lvl";
        std::filesystem::exists(ingame)) {
        library.add(read_file(ingame));
    }
    std::filesystem::create_directories(out_dir);
    const Terrain& terrain = level.terrain;
    const int samples = terrain.cells + 1;
    const std::filesystem::path texture_dir = out_dir / "textures";
    const std::filesystem::path model_dir = out_dir / "models";
    std::set<std::string> textures;  // written (lowercase names)
    // A texture's file relative to out_dir, written on first use; "" if
    // missing.
    const auto texture_file = [&](const std::string& name) -> std::string {
        const std::string key = lowercase(name);
        if (key.empty()) {
            return "";
        }
        if (!textures.contains(key)) {
            if (!export_texture(library, key, texture_dir / (key + ".png"))) {
                std::cerr << "texture " << name << ": not found or not decodable\n";
                return "";
            }
            textures.insert(key);
        }
        return "textures/" + key + ".png";
    };
    // A model's file relative to out_dir, written on first use; "" if missing.
    std::map<std::string, std::string> models;  // lowercase name -> file
    const auto model_file = [&](const std::string& name) -> std::string {
        const std::string key = lowercase(name);
        if (const auto it = models.find(key); it != models.end()) {
            return it->second;
        }
        std::string file;
        if (library.model(key) != nullptr) {
            try {
                const ModelExport written = export_model(library, key, model_dir, texture_dir, textures);
                file = "models/" + key + ".gltf";
                if (written.skipped > 0 || !written.missing_textures.empty()) {
                    std::cerr << std::format("model {}: {} segments, {} skipped, {} missing textures\n", key,
                                             written.segments, written.skipped, written.missing_textures.size());
                }
            } catch (const std::exception& error) {
                std::cerr << "model " << key << ": " << error.what() << "\n";
            }
        } else {
            std::cerr << "model " << key << ": not found\n";
        }
        models[key] = file;
        return file;
    };

    // Terrain bake, its texture layers (weights per sample in RGBA splat
    // maps, four layers each), baked lighting, and the map's sky and lights.
    {
        std::ofstream r32(out_dir / (map + ".r32"), std::ios::binary);
        r32.write(reinterpret_cast<const char*>(terrain.heights.data()), std::streamsize(terrain.heights.size() * 4));
        const size_t points = size_t(samples) * size_t(samples);
        std::vector<std::string> splat_maps;
        for (size_t first = 0; first < terrain.layer_weights.size(); first += 4) {
            swg::ImageRgba splat{uint32_t(samples), uint32_t(samples), std::vector<uint8_t>(points * 4, 0)};
            for (size_t p = 0; p < points; ++p) {
                for (size_t c = 0; c < 4 && first + c < terrain.layer_weights.size(); ++c) {
                    splat.pixels[p * 4 + c] = terrain.layer_weights[first + c][p];
                }
            }
            splat_maps.push_back(std::format("{}.splat{}.png", map, first / 4));
            swg::write_png(out_dir / splat_maps.back(), splat);
        }
        swg::ImageRgba light{uint32_t(samples), uint32_t(samples), std::vector<uint8_t>(points * 4, 255)};
        for (size_t p = 0; p < points; ++p) {
            std::copy(terrain.light[p].begin(), terrain.light[p].end(), light.pixels.begin() + std::ptrdiff_t(p * 4));
        }
        swg::write_png(out_dir / (map + ".light.png"), light);

        const auto [low, high] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
        const auto color = [](const std::array<float, 3>& c, float scale) {
            return std::format("[{:.3f}, {:.3f}, {:.3f}]", c[0] * scale, c[1] * scale, c[2] * scale);
        };
        std::ofstream json(out_dir / (map + ".json"), std::ios::binary);
        json << "{\n  \"source\": " << json_string(map + ".lvl (Battlefront II)") << ",\n  \"samples\": " << samples
             << ",\n  \"spacing\": " << terrain.unit << ",\n  \"origin_x\": " << terrain.origin()
             << ",\n  \"origin_z\": " << terrain.origin() << ",\n  \"min_height\": " << *low
             << ",\n  \"max_height\": " << *high
             << ",\n  \"format\": \"float32 little-endian, row-major, rows along +Z\",\n  \"texture_layers\": [";
        for (size_t i = 0; i < terrain.texture_layers.size(); ++i) {
            json << (i ? ", " : "") << json_string(terrain.texture_layers[i]);
        }
        // Splat and light maps: one pixel per sample, rows along +Z, x
        // (Battlefront's, not mirrored) along each row.
        json << "],\n  \"terrain_layers\": [";
        for (size_t i = 0; i < terrain.texture_layers.size(); ++i) {
            json << (i ? ",\n    " : "\n    ") << "{\"name\": " << json_string(terrain.texture_layers[i])
                 << ", \"texture\": " << json_string(texture_file(terrain.texture_layers[i]))
                 << ", \"scale\": " << terrain.texture_scales[i] << "}";
        }
        json << "\n  ],\n  \"splat_maps\": [";
        for (size_t i = 0; i < splat_maps.size(); ++i) {
            json << (i ? ", " : "") << json_string(splat_maps[i]);
        }
        json << "],\n  \"light_map\": " << json_string(map + ".light.png")
             << ",\n  \"detail_texture\": " << json_string(texture_file(terrain.detail_texture));

        // The sky and lights. The sun shines along its local +Z (ASSUMPTION),
        // turned by its rotation (w, x, y, z) in the paths' frame: negate z
        // for the world layers', then x for the game's.
        const LevelSky& sky = level.sky;
        json << ",\n  \"environment\": {\n    \"fog_color\": " << color(sky.fog_color, 1 / 255.0f)
             << ",\n    \"near_scene_range\": [";
        for (size_t i = 0; i < sky.near_scene_range.size(); ++i) {
            json << (i ? ", " : "") << sky.near_scene_range[i];
        }
        json << "],\n    \"ambient_top\": " << color(sky.ambient_top, 1 / 255.0f)
             << ",\n    \"ambient_bottom\": " << color(sky.ambient_bottom, 1 / 255.0f);
        if (!sky.dome_models.empty()) {
            json << ",\n    \"sky_dome\": " << json_string(model_file(sky.dome_models[0]));
        }
        const LevelLight* sun = nullptr;
        for (const LevelLight& l : level.lights) {
            if (l.type == 1 && (sun == nullptr || lowercase(l.name) == lowercase(sky.sun))) {
                sun = &l;
            }
        }
        if (sun != nullptr) {
            const auto [w, x, y, z] = sun->rotation;
            // q * (0, 0, 1) * q^-1: the rotation matrix's third column.
            const std::array<float, 3> along{2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
            json << std::format(",\n    \"sun\": {{\"direction\": [{:.4f}, {:.4f}, {:.4f}], \"color\": {}}}",
                                -along[0], along[1], -along[2], color(sun->color, 1));
        }
        json << ",\n    \"lights\": [";
        size_t lights = 0;
        for (const LevelLight& l : level.lights) {
            if (l.type != 2) {
                continue;  // omni lights only for now
            }
            json << (lights++ ? ",\n      " : "\n      ")
                 << std::format("{{\"position\": [{:.3f}, {:.3f}, {:.3f}], \"color\": {}, \"range\": {}}}",
                                -l.position[0], l.position[1], l.position[2], color(l.color, 1), l.range);
        }
        json << "\n    ]\n  }\n}\n";
        std::cerr << std::format("terrain: {} x {} samples, {} m apart, heights {:.2f}..{:.2f} m, {} filled holes, "
                                 "{} texture layers\n",
                                 samples, samples, terrain.unit, *low, *high, terrain.missing,
                                 terrain.texture_layers.size());
    }

    // Objects, in game space (x mirrored; rotations conjugated by the mirror).
    const std::string mode_layer = map + "_" + mode;
    const auto included = [&](const std::string& layer) {
        if (std::find(skipped.begin(), skipped.end(), layer) != skipped.end()) {
            return false;
        }
        for (const char* other : {"_conquest", "_ctf", "_1flag", "_2flag", "_flag_cps", "_eli", "_hunt", "_assault"}) {
            if (layer.find(other) != std::string::npos && layer != mode_layer) {
                return false;
            }
        }
        return true;
    };
    core::MapCollision collision;
    std::map<std::string, std::vector<CollisionTriangle>> shapes;  // model -> its collision
    {
        std::ofstream json(out_dir / (map + ".objects.json"), std::ios::binary);
        json << "{\n  \"map\": " << json_string(map) << ",\n  \"objects\": [";
        size_t written = 0;
        for (const WorldItem& object : level.objects) {
            if (!included(object.layer)) {
                continue;
            }
            // The rotation's stored triples are columns; mirroring x negates
            // the entries that mix x with y or z. `basis` is row-major.
            const auto& r = object.rotation;
            const std::array<float, 9> basis{r[0], -r[3], -r[6], -r[1], r[4], r[7], -r[2], r[5], r[8]};
            // The class's model (GeometryName), if it has one, and its
            // collision, placed in game space.
            std::string model;
            if (library.classes().find(object.type) != nullptr) {
                if (const auto geometry = library.classes().resolve(object.type).get("GeometryName")) {
                    model = model_file(*geometry);
                    auto shape = shapes.find(lowercase(*geometry));
                    if (shape == shapes.end()) {
                        shape = shapes.emplace(lowercase(*geometry), model_collision(library, *geometry)).first;
                    }
                    const auto place = [&](const std::array<float, 3>& p) {
                        const double x = -p[0], y = p[1], z = p[2];
                        return core::Vec3{basis[0] * x + basis[1] * y + basis[2] * z - object.position[0],
                                          basis[3] * x + basis[4] * y + basis[5] * z + object.position[1],
                                          basis[6] * x + basis[7] * y + basis[8] * z + object.position[2]};
                    };
                    for (const CollisionTriangle& t : shape->second) {
                        collision.triangles.push_back(
                            {place(t.a), place(t.b), place(t.c),
                             uint8_t((t.soldiers ? core::blocks_characters : 0) | (t.ordnance ? core::blocks_shots : 0))});
                    }
                }
            }
            json << (written++ ? ",\n" : "\n") << "    {\"layer\": " << json_string(object.layer)
                 << ", \"class\": " << json_string(object.type) << ", \"name\": " << json_string(object.name)
                 << ", \"model\": " << json_string(model)
                 << std::format(", \"position\": [{}, {}, {}], \"basis\": [", -object.position[0], object.position[1],
                                object.position[2]);
            for (size_t i = 0; i < 9; ++i) {
                json << (i ? ", " : "") << basis[i];
            }
            json << "]}";
        }
        json << "\n  ]\n}\n";
        std::cerr << std::format("objects: {} of {} placed; {} models, {} textures\n", written, level.objects.size(),
                                 models.size(), textures.size());
        core::save_map_collision(collision, core::map_collision_path(out_dir / map));
        std::cerr << std::format("collision: {} triangles\n", collision.triangles.size());
    }

    // The mode's command posts: capture regions, starting teams, spawn points.
    std::ofstream ini(out_dir / (map + "_" + mode + ".ini"), std::ios::binary);
    const double edge = terrain.origin();
    ini << "; " << map << "'s " << mode << " layer (" << mode_layer
        << "), written by bf2tool export-map from your own Battlefront II install.\n"
           "; Game data: don't commit or share it. Positions are Battlefront coordinates (x z).\n\n[map]\nName = "
        << map << "\nTerrain = " << map << "\n"
        << std::format("Bounds = {} {} {} {}\n", edge, edge, -edge, -edge);
    // The AI barriers that stop soldiers: places bots keep out of when they can.
    size_t barriers = 0;
    for (const LevelBarrier& barrier : level.barriers) {
        if (!included(barrier.layer) || !(barrier.flags & 1)) {
            continue;
        }
        ini << "Barrier =";
        for (const auto& corner : barrier.corners()) {
            ini << std::format(" {:.2f} {:.2f}", corner[0], corner[1]);
        }
        ini << "\n";
        ++barriers;
    }
    std::cerr << std::format("barriers: {} of {}\n", barriers, level.barriers.size());
    int posts = 0;
    for (const WorldItem& object : level.objects) {
        if (object.layer != mode_layer || object.get("CaptureRegion").empty()) {
            continue;
        }
        const WorldItem* capture = level.region(object.get("CaptureRegion"));
        const auto& at = capture ? capture->position : object.position;
        ini << "\n[post]\nName = " << (object.get("Label").empty() ? object.name : object.get("Label")) << "\n"
            << std::format("Position = {:.2f} {:.2f}\n", at[0], at[2]);
        if (capture != nullptr) {
            // ASSUMPTION: a capture cylinder's SIZE x is its radius.
            ini << std::format("Radius = {:.2f}\n", std::max(capture->size[0], capture->size[2]));
        }
        if (const std::string team = object.get("Team"); team == "1" || team == "2") {
            ini << "Owner = " << team << "\n";
        }
        if (const LevelPath* spawns = level.path(object.get("SpawnPath"), mode_layer)) {
            for (const PathNode& node : spawns->nodes) {
                ini << std::format("SpawnPoint = {:.2f} {:.2f}\n", node.position[0], node.position[2]);
            }
        }
        ++posts;
    }
    std::cerr << std::format("{}: {} command posts\nwrote {}\n", mode_layer, posts, out_dir.string());
    if (posts == 0) {
        throw std::runtime_error("no command posts in layer " + mode_layer);
    }
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
        if (command == "export-loadout") {
            return cmd_export_loadout(args);
        }
        if (command == "export-map") {
            return cmd_export_map(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
