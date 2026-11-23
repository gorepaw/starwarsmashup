// bf2tool: inspect Battlefront II munged files (.lvl).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "bf2/animation.h"
#include "bf2/effect.h"
#include "bf2/hash.h"
#include "bf2/hud.h"
#include "bf2/level.h"
#include "bf2/model_export.h"
#include "bf2/odf.h"
#include "bf2/sound.h"
#include "bf2/ucfb.h"
#include "swg/image.h"
#include "swm/core/conquest.h"
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
                     [--death <region>]... [--open <object>]... [--team <post>=<n>]... [--disable <barrier>]...
                     [--drop-boundary <path>]... [--set <object>.<property>=<value>]...
                     [--regen <object>=<health a second>]... [--kill-plays <object>=<animation group>]...
                     [--kill-stops <object>=<animation group>]... [--kill-removes <object>=<death region>]...
                     [--start-plays <animation group>]... [--timed-death <region>=<first>,<on>,<off>]...
  bf2tool export-hud <_lvl_pc dir> -o <out dir>
  bf2tool export-effects <_lvl_pc dir> -o <out dir> [--map <map>]...
  bf2tool export-audio <_lvl_pc dir> -o <out dir>
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

// First whitespace-delimited token (strips trailing " defer" flags on sound names).
std::string first_token(std::string_view text) {
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }
    const size_t end = text.find_first_of(" \t\r\n", start);
    return std::string(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
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
                            "SpreadLimit", "ShotsPerSalvo", "ShotElevate", "MaxRange", "ZoomMin", "ZoomMax",
                            "ZoomFirstPerson", "MuzzleFlash", "FlashColor",
                            "FlashLength", "FlashLightColor", "FlashLightRadius", "FlashLightDuration"}) {
        if (const auto value = weapon.get(key)) {
            out << key << " = " << *value << "\n";
        }
    }
    std::string fire_sound = first_token(weapon.get("FireSound").value_or(""));
    if (fire_sound.empty()) {
        fire_sound = first_token(weapon.get("FireLoopSound").value_or(""));
    }
    if (!fire_sound.empty()) {
        out << "FireSound = " << fire_sound << "\n";
    }
    for (const char* key : {"FireEmptySound", "ReloadSound", "OverheatSound", "OverheatStopSound"}) {
        if (const auto value = weapon.get(key)) {
            const std::string tok = first_token(*value);
            if (!tok.empty()) {
                out << key << " = " << tok << "\n";
            }
        }
    }
    std::string change_sound = first_token(weapon.get("WeaponChangeSound").value_or(""));
    if (change_sound.empty()) {
        change_sound = first_token(weapon.get("ChangeModeSound").value_or(""));
    }
    if (!change_sound.empty()) {
        out << "WeaponChangeSound = " << change_sound << "\n";
    }
    out << "; ordnance: " << ordnance.type << " (" << ordnance.base << ")\n";
    for (const char* key : {"MaxDamage", "PersonScale", "Velocity", "Acceleration", "Gravity", "Rebound", "LifeSpan",
                            "DamageTransitionDelay", "DamageTransitionPeriod", "DamageFinalDamage",
                            "LaserTexture", "LaserGlowColor", "LaserLength", "LaserWidth", "GlowLength", "BlurLength",
                            "LightColor", "LightRadius", "GeometryName", "TrailEffect"}) {
        if (const auto value = ordnance.get(key)) {
            out << key << " = " << *value << "\n";
        }
    }
    for (const char* key : {"OrdnanceSound", "CollisionOtherSound"}) {
        if (const auto value = ordnance.get(key)) {
            const std::string tok = first_token(*value);
            if (!tok.empty()) {
                out << key << " = " << tok << "\n";
            }
        }
    }
    std::string impact_effect;
    for (const char* key : {"ImpactEffectSoft", "ImpactEffectTerrain", "ImpactEffectRigid", "ImpactEffectStatic",
                            "ImpactEffect"}) {
        if (const auto value = ordnance.get(key); value && !value->empty()) {
            impact_effect = *value;
            break;
        }
    }
    if (!impact_effect.empty()) {
        out << "ImpactEffect = " << impact_effect << "\n";
    }
    if (projectile == "grenade") {
        // Its bounce (Friction; Rebound is above) and when its timer (LifeSpan)
        // starts. A "sticky" grenade (the thermal detonator) says nothing of
        // StartTimerOnContact. ASSUMPTION: its timer starts when it lands
        // (0.6 s from the throw would go off in the air).
        if (const auto friction = ordnance.get("Friction")) {
            out << "Friction = " << *friction << "\n";
        }
        const std::string contact =
            ordnance.get("StartTimerOnContact").value_or(lowercase(ordnance.base) == "sticky" ? "1" : "0");
        out << "StartTimerOnContact = " << contact << "\n";
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
                                    std::pair{"PersonScale", "ExplosionPersonScale"},
                                    std::pair{"Push", "ExplosionPush"},
                                    std::pair{"PushRadiusInner", "ExplosionPushRadiusInner"},
                                    std::pair{"PushRadiusOuter", "ExplosionPushRadiusOuter"},
                                    std::pair{"Effect", "ExplosionEffect"}}) {
        if (const auto value = explosion.get(key)) {
            out << ours << " = " << *value << "\n";
        }
    }
    if (const auto sound = explosion.get("SoundProperty")) {
        const std::string tok = first_token(*sound);
        if (!tok.empty()) {
            out << "ExplosionSound = " << tok << "\n";
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
// file for battlefields (<map>_<mode>.ini), with its boundary and the death
// regions named by --death (the mission script's AddDeathRegion). Each game
// mode is a sublevel; other modes' are left out, and so are any --skip
// layers (e.g. the other era's props) and --open objects (by name: ones
// fought open, Kashyyyk's wooden barricades, or killed by the mission script,
// KillObject: left out as if destroyed, and a command post with them). Like
// the script, --team <post>=<n> sets a post's starting team (SetObjectTeam)
// and --disable <name> drops the AI barriers of that name (DisableBarriers).
// --drop-boundary <path> leaves a boundary path out (the Death Star's cuts
// through the station's south end, where two of its posts are; sealed,
// with its falls death regions, it needs none).
int cmd_export_map(const std::vector<std::string>& args) {
    if (args.size() < 4 || args[2] != "-o") {
        std::cerr << usage;
        return 2;
    }
    const std::string map = args[1];
    const std::filesystem::path out_dir = args[3];
    std::string mode = "conquest";
    std::vector<std::string> skipped;
    std::vector<std::string> deaths;
    std::vector<std::string> opened;
    std::vector<std::string> disabled;
    std::vector<std::string> dropped_boundaries;
    // What the mission script does when an object is destroyed (OnObjectKill)
    // and stands again (OnObjectRespawn), by the object's name (lowercase):
    // a world animation group it plays (and rewinds), a looping one it
    // pauses, a death region it removes; and how fast the object mends.
    std::map<std::string, double> regens;
    std::map<std::string, std::string> kill_plays, kill_stops, kill_removes;
    // Groups the script plays at the start (lowercase): what they move stands
    // where they leave it. Death regions it turns on and off on a timer:
    // first on after `first` s, for `on` s, then off for `off` s, and again.
    std::vector<std::string> start_plays;
    std::map<std::string, std::array<double, 3>> timed_deaths;
    std::map<std::string, std::string> teams;  // post name (lowercase) -> team
    // object name (lowercase) -> property, value: the mission script's
    // SetProperty (a door's IsLocked, a post's SpawnPath or CaptureRegion)
    std::vector<std::tuple<std::string, std::string, std::string>> sets;
    for (size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--mode") {
            mode = args[i + 1];
        } else if (args[i] == "--skip") {
            skipped.push_back(args[i + 1]);
        } else if (args[i] == "--death") {
            deaths.push_back(args[i + 1]);
        } else if (args[i] == "--open") {
            opened.push_back(lowercase(args[i + 1]));
        } else if (args[i] == "--disable") {
            disabled.push_back(lowercase(args[i + 1]));
        } else if (args[i] == "--drop-boundary") {
            dropped_boundaries.push_back(lowercase(args[i + 1]));
        } else if (args[i] == "--team") {
            const size_t eq = args[i + 1].find('=');
            if (eq == std::string::npos) {
                std::cerr << usage;
                return 2;
            }
            teams[lowercase(args[i + 1].substr(0, eq))] = args[i + 1].substr(eq + 1);
        } else if (args[i] == "--start-plays") {
            start_plays.push_back(lowercase(args[i + 1]));
        } else if (args[i] == "--timed-death") {
            const std::string& value = args[i + 1];
            const size_t eq = value.find('=');
            std::array<double, 3> timing{};
            char comma1 = 0, comma2 = 0;
            if (eq == std::string::npos ||
                !(std::istringstream(value.substr(eq + 1)) >> timing[0] >> comma1 >> timing[1] >> comma2 >> timing[2]) ||
                comma1 != ',' || comma2 != ',') {
                std::cerr << usage;
                return 2;
            }
            timed_deaths[lowercase(value.substr(0, eq))] = timing;
            deaths.push_back(value.substr(0, eq));
        } else if (args[i] == "--regen" || args[i] == "--kill-plays" || args[i] == "--kill-stops" ||
                   args[i] == "--kill-removes") {
            const size_t eq = args[i + 1].find('=');
            if (eq == std::string::npos) {
                std::cerr << usage;
                return 2;
            }
            const std::string object = lowercase(args[i + 1].substr(0, eq)), value = args[i + 1].substr(eq + 1);
            if (args[i] == "--regen") {
                regens[object] = std::atof(value.c_str());
            } else {
                (args[i] == "--kill-plays" ? kill_plays : args[i] == "--kill-stops" ? kill_stops : kill_removes)[object] =
                    lowercase(value);
            }
        } else if (args[i] == "--set") {
            const std::string& set = args[i + 1];
            const size_t dot = set.find('.'), eq = set.find('=');
            if (dot == std::string::npos || eq == std::string::npos || eq < dot) {
                std::cerr << usage;
                return 2;
            }
            sets.emplace_back(lowercase(set.substr(0, dot)), set.substr(dot + 1, eq - dot - 1), set.substr(eq + 1));
        }
    }
    const std::string folder = map.substr(0, 3);
    std::vector<uint8_t> bytes = read_file(std::filesystem::path(args[0]) / folder / (map + ".lvl"));
    Level level = read_level(parse_ucfb(bytes));
    // What the mission script sets, over what the map says (the last value
    // of a property is the one read).
    for (const auto& [name, property, value] : sets) {
        size_t found = 0;
        for (WorldItem& object : level.objects) {
            if (lowercase(object.name) == name) {
                object.properties.emplace_back(odf_hash(property), value);
                ++found;
            }
        }
        if (found == 0) {
            std::cerr << std::format("set: no object {}\n", name);
        }
    }
    // Models and textures: the map's own, then the shared props' (ingame.lvl).
    AssetLibrary library;
    library.add(std::move(bytes));
    if (const std::filesystem::path ingame = std::filesystem::path(args[0]) / "ingame.lvl";
        std::filesystem::exists(ingame)) {
        library.add(read_file(ingame));
    }
    std::filesystem::create_directories(out_dir);
    // A map without terrain (aboard Tantive IV): a flat stand-in under
    // everything, the game's ground to fall to, not drawn (`hidden` in the
    // json), with a death region over it (below).
    const bool no_terrain = level.terrain.cells == 0;
    Terrain stand_in;
    double fall_y = 0;
    // The water's rectangles (Battlefront x0 z0 x1 z1) and heights.
    std::vector<std::array<double, 5>> water_boxes;
    if (no_terrain) {
        double reach = 0, low = 1e9;
        for (const WorldItem& object : level.objects) {
            reach = std::max({reach, std::abs(double(object.position[0])), std::abs(double(object.position[2]))});
            low = std::min(low, double(object.position[1]));
        }
        stand_in.name = map;
        stand_in.unit = 8;
        stand_in.cells = std::max(8, int(std::ceil((reach + 64) * 2 / stand_in.unit / 8)) * 8);
        fall_y = (low < 1e8 ? low : 0) - 60;
        const size_t points = size_t(stand_in.cells + 1) * size_t(stand_in.cells + 1);
        stand_in.heights.assign(points, float(fall_y));
        stand_in.light.assign(points, {128, 128, 128});
        std::cerr << std::format("terrain: none; a stand-in {} m square at {:.1f} m\n", stand_in.cells * stand_in.unit,
                                 fall_y);
    }
    const Terrain& terrain = no_terrain ? stand_in : level.terrain;
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
    const auto model_file = [&](const std::string& name, const std::vector<ModelClip>& clips = {}) -> std::string {
        const std::string key = lowercase(name);
        if (const auto it = models.find(key); it != models.end()) {
            return it->second;
        }
        std::string file;
        if (library.model(key) != nullptr) {
            try {
                const ModelExport written = export_model(library, key, model_dir, texture_dir, textures, clips);
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
        // Its cutters, in game space (x mirrored: a plane's normal x
        // negated), one per line: center x y z, radius, then each plane's
        // nx ny nz d (core::load_bake).
        {
            std::ofstream cuts(out_dir / (map + ".cuts"), std::ios::binary);
            cuts << "# " << map << "'s terrain cutters: center x y z, radius, then planes (nx ny nz d), game space\n";
            for (const TerrainCut& cut : terrain.cuts) {
                cuts << std::format("{:.3f} {:.3f} {:.3f} {:.3f}", -cut.center[0], cut.center[1], cut.center[2],
                                    cut.radius);
                for (const auto& plane : cut.planes) {
                    cuts << std::format("  {:.5f} {:.5f} {:.5f} {:.4f}", -plane[0], plane[1], plane[2], plane[3]);
                }
                cuts << "\n";
            }
        }
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
             << ",\n  \"spacing\": " << terrain.unit << ",\n  \"origin_x\": " << terrain.origin_x()
             << ",\n  \"origin_z\": " << terrain.origin_z() << ",\n  \"min_height\": " << *low
             << ",\n  \"max_height\": " << *high << (no_terrain ? ",\n  \"hidden\": true" : "")
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
        // The HUD's minimap image, north up: <map>_map, or named for the
        // map's world layers (Yavin's yavin1_map, its layers yavin1_*).
        // Where it lies isn't stored with it: a battlefield file sets that
        // (Minimap).
        std::string minimap = map + "_map";
        for (const std::string& layer : level.layers) {
            if (library.texture(lowercase(minimap)) != nullptr) {
                break;
            }
            minimap = layer.substr(0, layer.find('_')) + "_map";
        }
        if (library.texture(lowercase(minimap)) == nullptr) {
            minimap = map + "_map";
        }
        json << ",\n  \"minimap\": " << json_string(texture_file(minimap));

        // The sky and lights. The sun shines along its local +Z (ASSUMPTION),
        // turned by its rotation (w, x, y, z) in the paths' frame: negate z
        // for the world layers', then x for the game's.
        const LevelSky& sky = level.sky;
        json << ",\n  \"environment\": {\n    \"fog\": " << (sky.fog ? "true" : "false")
             << ",\n    \"fog_color\": " << color(sky.fog_color, 1 / 255.0f)
             << ",\n    \"near_scene_range\": [";
        for (size_t i = 0; i < sky.near_scene_range.size(); ++i) {
            json << (i ? ", " : "") << sky.near_scene_range[i];
        }
        json << "],\n    \"ambient_top\": " << color(sky.ambient_top, 1 / 255.0f)
             << ",\n    \"ambient_bottom\": " << color(sky.ambient_bottom, 1 / 255.0f);
        // The dome, and the models drawn with it: each with how far down,
        // how far it follows the camera and how fast it turns.
        if (!sky.dome_models.empty()) {
            json << ",\n    \"sky_dome\": " << json_string(model_file(sky.dome_models[0].geometry));
            json << ",\n    \"sky_domes\": [";
            for (size_t i = 0; i < sky.dome_models.size(); ++i) {
                const SkyDomeModel& dome = sky.dome_models[i];
                json << (i ? ", " : "")
                     << std::format("{{\"model\": {}, \"offset\": {}, \"movement_scale\": {}, \"rotation_speed\": {}}}",
                                    json_string(model_file(dome.geometry)), dome.offset, dome.movement_scale,
                                    dome.rotation_speed);
            }
            json << "]";
        }
        const LevelLight* sun = nullptr;
        for (const LevelLight& l : level.lights) {
            if (l.type == 1 && (sun == nullptr || lowercase(l.name) == lowercase(sky.sun))) {
                sun = &l;
            }
        }
        const auto direction = [](const LevelLight& l) {
            const auto [w, x, y, z] = l.rotation;
            // q * (0, 0, 1) * q^-1: the rotation matrix's third column.
            const std::array<float, 3> along{2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
            return std::format("[{:.4f}, {:.4f}, {:.4f}]", -along[0], along[1], -along[2]);
        };
        if (sun != nullptr) {
            json << std::format(",\n    \"sun\": {{\"direction\": {}, \"color\": {}, \"baked\": {}}}", direction(*sun),
                                color(sun->color, 1), sun->baked ? "true" : "false");
        }
        // The second global light (Light2), on what the baked light isn't:
        // soldiers. Coruscant's temple has no ambient light; its "BACK" light
        // is what shows soldiers indoors, out of the sun.
        for (const LevelLight& l : level.lights) {
            if (l.type == 1 && &l != sun && !sky.back.empty() && lowercase(l.name) == lowercase(sky.back)) {
                json << std::format(",\n    \"back\": {{\"direction\": {}, \"color\": {}}}", direction(l),
                                    color(l.color, 1));
                break;
            }
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
        json << "\n    ]";

        // The fog cloud: big soft sprites drifting at a height (Effect("FogCloud")).
        const auto numbers = [](const LevelEffect* effect, const char* property) {
            const ConfigRecord* record = effect ? effect->get(property) : nullptr;
            return record ? record->numbers : std::vector<float>{};
        };
        const auto number_list = [](const std::vector<float>& values, float scale) {
            std::string out = "[";
            for (size_t i = 0; i < values.size(); ++i) {
                out += std::format("{}{}", i ? ", " : "", values[i] * scale);
            }
            return out + "]";
        };
        const auto text_of = [](const LevelEffect* effect, const char* property) {
            const ConfigRecord* record = effect ? effect->get(property) : nullptr;
            return record ? record->text : std::string();
        };
        const LevelEffect* cloud = level.effect("FogCloud");
        if (cloud != nullptr && !numbers(cloud, "Enable").empty() && numbers(cloud, "Enable")[0] != 0) {
            const auto first = [&](const char* property, float otherwise) {
                const std::vector<float> values = numbers(cloud, property);
                return values.empty() ? otherwise : values[0];
            };
            std::vector<float> velocity = numbers(cloud, "Velocity");
            velocity.resize(2, 0.0f);
            json << ",\n    \"fog_cloud\": {\"texture\": " << json_string(texture_file(text_of(cloud, "Texture")))
                 << ", \"range\": " << number_list(numbers(cloud, "Range"), 1)
                 << ", \"color\": " << number_list(numbers(cloud, "Color"), 1 / 255.0f)
                 << std::format(", \"velocity\": [{}, {}]", -velocity[0], velocity[1])
                 << std::format(", \"rotation\": {}, \"height\": {}, \"particle_size\": {}, \"density\": {}}}",
                                first("Rotation", 0), first("Height", 0), first("ParticleSize", 1),
                                first("ParticleDensity", 0));
        }
        json << "\n  }";

        // Water: each layer's surface as rectangles in game space (runs of
        // wet cells, merged down the rows), within the terrain; the look from
        // Effect("Water").
        const LevelWater& water = level.water;
        size_t rectangles = 0;
        if (!water.layers.empty()) {
            const LevelEffect* look = level.effect("Water");
            json << ",\n  \"water\": {\n    \"texture\": " << json_string(texture_file(text_of(look, "MainTexture")))
                 << ",\n    \"cell_size\": " << water.cell_size
                 << ",\n    \"tile\": " << number_list(numbers(look, "Tile"), 1)
                 << ",\n    \"velocity\": " << number_list(numbers(look, "Velocity"), 1)
                 << ",\n    \"refraction_color\": " << number_list(numbers(look, "RefractionColor"), 1 / 255.0f)
                 << ",\n    \"reflection_color\": " << number_list(numbers(look, "ReflectionColor"), 1 / 255.0f)
                 << ",\n    \"underwater_color\": " << number_list(numbers(look, "UnderwaterColor"), 1 / 255.0f)
                 << ",\n    \"fresnel\": " << number_list(numbers(look, "FresnelMinMax"), 1);
            // Animated normal maps: a name prefix, a frame count and a rate.
            const ConfigRecord* normals = look ? look->get("NormalMapTextures") : nullptr;
            json << ",\n    \"normal_maps\": [";
            if (normals != nullptr && !normals->numbers.empty()) {
                const int frames = std::clamp(int(normals->numbers[0]), 0, 64);
                for (int i = 0; i < frames; ++i) {
                    json << (i ? ", " : "") << json_string(texture_file(normals->text + std::to_string(i)));
                }
            }
            json << "],\n    \"normal_map_rate\": "
                 << (normals != nullptr && normals->numbers.size() >= 2 ? normals->numbers[1] : 0.0f);
            json << ",\n    \"surfaces\": [";
            const double extent = terrain.cells * terrain.unit;
            const double low_x = terrain.origin_x(), high_x = low_x + extent;
            const double low_z = terrain.origin_z(), high_z = low_z + extent;
            for (size_t layer = 0; layer < water.layers.size(); ++layer) {
                const WaterLayer& surface = water.layers[layer];
                // Runs per row as [first, last] columns, then rows merged
                // while a run repeats.
                struct Run {
                    int first, last, row_first, row_last;
                };
                std::vector<Run> open, done;
                for (int row = 0; row < water.cells; ++row) {
                    std::vector<Run> next;
                    for (int column = 0; column < water.cells;) {
                        if (water.at(column, row) != &surface) {
                            ++column;
                            continue;
                        }
                        int last = column;
                        while (last + 1 < water.cells && water.at(last + 1, row) == &surface) {
                            ++last;
                        }
                        Run run{column, last, row, row};
                        for (const Run& above : open) {
                            if (above.first == column && above.last == last && above.row_last == row - 1) {
                                run.row_first = above.row_first;
                            }
                        }
                        next.push_back(run);
                        column = last + 1;
                    }
                    for (const Run& above : open) {
                        const bool continued = std::any_of(next.begin(), next.end(), [&](const Run& r) {
                            return r.row_first == above.row_first && r.first == above.first && r.last == above.last;
                        });
                        if (!continued) {
                            done.push_back(above);
                        }
                    }
                    open = next;
                }
                done.insert(done.end(), open.begin(), open.end());
                json << (layer ? ",\n      " : "\n      ") << "{\"name\": " << json_string(surface.name)
                     << ", \"height\": " << surface.height
                     << std::format(", \"color\": [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", surface.color[0] / 255.0f,
                                    surface.color[1] / 255.0f, surface.color[2] / 255.0f, surface.color[3] / 255.0f)
                     << ", \"rectangles\": [";
                size_t written = 0;
                for (const Run& run : done) {
                    // Battlefront x and z, clipped to the terrain; game x = -x.
                    const double x0 = std::max(low_x, double(water.origin[0]) + run.first * double(water.cell_size));
                    const double x1 = std::min(high_x, double(water.origin[0]) + (run.last + 1) * double(water.cell_size));
                    const double z0 = std::max(low_z, double(water.origin[1]) + run.row_first * double(water.cell_size));
                    const double z1 =
                        std::min(high_z, double(water.origin[1]) + (run.row_last + 1) * double(water.cell_size));
                    if (x0 >= x1 || z0 >= z1) {
                        continue;
                    }
                    json << (written++ ? ", " : "") << std::format("[{}, {}, {}, {}]", -x1, z0, -x0, z1);
                    water_boxes.push_back({x0, z0, x1, z1, double(surface.height)});
                }
                json << "]}";
                rectangles += written;
            }
            json << "\n    ]\n  }";
        }
        json << "\n}\n";
        std::cerr << std::format("terrain: {} x {} samples, {} m apart, heights {:.2f}..{:.2f} m, {} filled holes, "
                                 "{} texture layers\n",
                                 samples, samples, terrain.unit, *low, *high, terrain.missing,
                                 terrain.texture_layers.size());
        std::cerr << std::format("water: {} layers, {} rectangles; fog cloud: {}\n", water.layers.size(), rectangles,
                                 cloud != nullptr ? "yes" : "no");
    }

    // The game mode's sublevel. Its layers are named for the mode
    // (dag1_conquest, tat2_con, myg1_cp-conquest; with others, like
    // hoth_cps). Failing that (Felucia's and Mustafar's conquest is a lone
    // "_design" layer), it's the one sublevel with command posts that no
    // other mode's name claims. Other sublevels without command posts are
    // era props (Dagobah's X-wing), kept unless skipped.
    const auto ends_with = [](const std::string& text, std::string_view end) {
        return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
    };
    const std::vector<std::string> mode_names =
        mode == "conquest" ? std::vector<std::string>{"_conquest", "_con", "_cp-conquest"}
                           : std::vector<std::string>{"_" + mode};
    const auto named_for_mode = [&](const std::string& layer) {
        const std::string name = lowercase(layer);
        return std::any_of(mode_names.begin(), mode_names.end(), [&](const std::string& m) { return ends_with(name, m); });
    };
    const auto named_for_other_mode = [&](const std::string& layer) {
        const std::string name = lowercase(layer);
        for (const char* other : {"conquest", "_con", "ctf", "flag", "_eli", "hunt", "tdm", "campaign", "objective",
                                  "_obj", "assault", "assult", "xl"}) {
            if (name.find(other) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    std::set<uint32_t> post_sublevels;  // sublevels with command posts
    for (const WorldItem& object : level.objects) {
        if (object.sublevel != 0 && !object.get("CaptureRegion").empty()) {
            post_sublevels.insert(object.sublevel);
        }
    }
    uint32_t mode_sublevel = 0;
    std::string mode_layers;  // its layers, for the map file's header
    for (int pass = 0; pass < 2 && mode_sublevel == 0; ++pass) {
        for (const auto& [hash, layers] : level.sublevels) {
            const bool chosen =
                pass == 0 ? std::any_of(layers.begin(), layers.end(), named_for_mode)
                          : post_sublevels.contains(hash) && std::none_of(layers.begin(), layers.end(), named_for_other_mode);
            if (chosen) {
                mode_sublevel = hash;
                for (const std::string& layer : layers) {
                    mode_layers += (mode_layers.empty() ? "" : ", ") + layer;
                }
                break;
            }
        }
    }
    if (mode_sublevel == 0) {
        throw std::runtime_error("no " + mode + " sublevel");
    }
    std::set<uint32_t> modes;  // every game mode's sublevel
    for (const auto& [hash, layers] : level.sublevels) {
        if (post_sublevels.contains(hash) || std::any_of(layers.begin(), layers.end(), named_for_other_mode)) {
            modes.insert(hash);
        }
    }
    const auto included_in = [&](const std::string& layer, uint32_t sublevel) {
        if (std::find(skipped.begin(), skipped.end(), layer) != skipped.end()) {
            return false;
        }
        return sublevel == 0 || sublevel == mode_sublevel || !modes.contains(sublevel);
    };
    const auto included = [&](const auto& item) { return included_in(item.layer, item.sublevel); };
    // Paths of what's included: base layers' are grouped by their layer's
    // name hash, a sublevel's by its hash.
    std::set<uint32_t> path_groups;
    for (const auto& [layer, group] : level.layer_groups) {
        if (group == odf_hash(layer) && std::find(skipped.begin(), skipped.end(), layer) == skipped.end()) {
            path_groups.insert(group);
        }
    }
    for (const auto& [hash, layers] : level.sublevels) {
        if (hash == mode_sublevel || !modes.contains(hash)) {
            path_groups.insert(hash);
        }
    }

    // What can be destroyed, in the order the objects come (their indices),
    // as the objects below go.
    std::map<std::string, int> destructible_index;  // object name (lowercase) -> index
    for (const WorldItem& object : level.objects) {
        if (!included(object) || std::find(opened.begin(), opened.end(), lowercase(object.name)) != opened.end() ||
            library.classes().find(object.type) == nullptr) {
            continue;
        }
        const OdfClass resolved = library.classes().resolve(object.type);
        if (resolved.get("GeometryName") && lowercase(resolved.base) == "destructablebuilding" &&
            std::atof(resolved.get("MaxHealth").value_or("0").c_str()) > 0) {
            const int index = int(destructible_index.size());
            destructible_index.emplace(lowercase(object.name), index);
        }
    }
    // World animations by the object they move (lowercase): its group, and
    // the destructible whose fate plays or stops it (-1: none) and how.
    struct ObjectAnimation {
        const WorldAnimationGroup* group;
        const WorldAnimation* animation;
        int trigger = -1;
        std::string mode;  // "plays" or "stops", with a trigger; "start": played at the start (stands at its end)
    };
    std::map<std::string, std::vector<ObjectAnimation>> object_animations;
    std::set<std::string> groups_seen;  // the same group in several included layers: once
    for (const WorldAnimationGroup& group : level.animation_groups) {
        if (!included(group) || !groups_seen.insert(lowercase(group.name)).second) {
            continue;
        }
        ObjectAnimation found{&group, nullptr};
        if (std::find(start_plays.begin(), start_plays.end(), lowercase(group.name)) != start_plays.end()) {
            found.mode = "start";
        }
        for (const auto& [table, how] : {std::pair{&kill_plays, "plays"}, std::pair{&kill_stops, "stops"}}) {
            for (const auto& [object, name] : *table) {
                if (name == lowercase(group.name) && destructible_index.contains(object)) {
                    found.trigger = destructible_index[object];
                    found.mode = how;
                }
            }
        }
        for (const auto& [animation_name, object] : group.members) {
            for (const WorldAnimation& animation : level.animations) {
                if (lowercase(animation.name) == lowercase(animation_name)) {
                    found.animation = &animation;
                    object_animations[lowercase(object)].push_back(found);
                    break;  // the same animation in several layers: the first
                }
            }
        }
    }

    // Objects, in game space (x mirrored; rotations conjugated by the mirror).
    core::MapCollision collision;
    std::vector<std::string> powerups;  // the health and ammo droids' lines for the map file
    std::vector<std::string> doors;     // the doors' lines (core::Door)
    std::vector<std::string> destructibles;  // what can be destroyed: lines (core::Destructible)
    std::map<std::string, std::vector<CollisionTriangle>> shapes;  // model -> its collision
    {
        std::ofstream json(out_dir / (map + ".objects.json"), std::ios::binary);
        json << "{\n  \"map\": " << json_string(map) << ",\n  \"objects\": [";
        size_t written = 0;
        size_t left_open = 0;
        size_t doors_open = 0, doors_shut = 0;
        for (const WorldItem& object : level.objects) {
            if (!included(object)) {
                continue;
            }
            if (std::find(opened.begin(), opened.end(), lowercase(object.name)) != opened.end()) {
                ++left_open;
                continue;
            }
            // The rotation's stored triples are columns; mirroring x negates
            // the entries that mix x with y or z. `basis` is row-major.
            const auto& r = object.rotation;
            const std::array<float, 9> basis{r[0], -r[3], -r[6], -r[1], r[4], r[7], -r[2], r[5], r[8]};
            // Where a world animation leaves it at a time: moved (game space)
            // and turned in its own axes (game space, row-major). Keys are
            // linear between, held past the ends; a rotation key's angles
            // turn about x, then y, then z (ASSUMPTION, as the client has it).
            struct Pose {
                core::Vec3 offset;
                std::array<double, 9> turn{1, 0, 0, 0, 1, 0, 0, 0, 1};
            };
            const auto pose_at = [&](const WorldAnimation& animation, float time) {
                const auto sample = [&](const std::vector<WorldAnimation::Key>& keys) {
                    std::array<float, 3> v{};
                    for (size_t k = 0; k < keys.size(); ++k) {
                        if (k == 0 || time >= keys[k].time) {
                            v = keys[k].value;
                        }
                        if (k > 0 && time > keys[k - 1].time && time < keys[k].time) {
                            const float t = (time - keys[k - 1].time) / (keys[k].time - keys[k - 1].time);
                            for (size_t i = 0; i < 3; ++i) {
                                v[i] = keys[k - 1].value[i] + (keys[k].value[i] - keys[k - 1].value[i]) * t;
                            }
                            break;
                        }
                    }
                    return v;
                };
                Pose pose;
                const auto o = sample(animation.positions);
                const core::Vec3 m{-o[0], o[1], o[2]};
                pose.offset = animation.local ? core::Vec3{basis[0] * m.x + basis[1] * m.y + basis[2] * m.z,
                                                           basis[3] * m.x + basis[4] * m.y + basis[5] * m.z,
                                                           basis[6] * m.x + basis[7] * m.y + basis[8] * m.z}
                                              : m;
                const auto e = sample(animation.rotations);
                const double ax = e[0], ay = -e[1], az = -e[2];  // mirrored
                const std::array<double, 9> rx{1, 0, 0, 0, std::cos(ax), -std::sin(ax), 0, std::sin(ax), std::cos(ax)};
                const std::array<double, 9> ry{std::cos(ay), 0, std::sin(ay), 0, 1, 0, -std::sin(ay), 0, std::cos(ay)};
                const std::array<double, 9> rz{std::cos(az), -std::sin(az), 0, std::sin(az), std::cos(az), 0, 0, 0, 1};
                const auto mul = [](const std::array<double, 9>& a, const std::array<double, 9>& b) {
                    std::array<double, 9> c{};
                    for (int i = 0; i < 3; ++i) {
                        for (int j = 0; j < 3; ++j) {
                            for (int k = 0; k < 3; ++k) {
                                c[size_t(i * 3 + j)] += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
                            }
                        }
                    }
                    return c;
                };
                pose.turn = mul(rz, mul(ry, rx));
                return pose;
            };
            const auto last_time = [](const WorldAnimation& animation) {
                float last = 0;
                for (const auto* keys : {&animation.positions, &animation.rotations}) {
                    for (const auto& key : *keys) {
                        last = std::max(last, key.time);
                    }
                }
                return last;
            };
            // Where it stands: where a group the script plays at the start
            // leaves it (unless something destroyed moves it too: its keys
            // say where it stands then).
            Pose rest;
            const ObjectAnimation* kill_moves = nullptr;
            if (const auto it = object_animations.find(lowercase(object.name)); it != object_animations.end()) {
                for (const ObjectAnimation& a : it->second) {
                    if (a.mode == "plays") {
                        kill_moves = &a;
                    }
                }
                for (const ObjectAnimation& a : it->second) {
                    if (a.mode == "start" && kill_moves == nullptr) {
                        rest = pose_at(*a.animation, last_time(*a.animation));
                    }
                }
            }
            // ...its game-space basis and position, so posed.
            std::array<double, 9> rest_basis{};
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    for (int k = 0; k < 3; ++k) {
                        rest_basis[size_t(i * 3 + j)] += basis[size_t(i * 3 + k)] * rest.turn[size_t(k * 3 + j)];
                    }
                }
            }
            const core::Vec3 rest_position = core::Vec3{-object.position[0], object.position[1], object.position[2]} + rest.offset;
            // A model point (Battlefront's) in game space, posed.
            const auto posed = [&](const std::array<float, 3>& p, const Pose& pose) {
                const double x = -p[0], y = p[1], z = p[2];
                const auto& t = pose.turn;
                const double tx = t[0] * x + t[1] * y + t[2] * z, ty = t[3] * x + t[4] * y + t[5] * z,
                             tz = t[6] * x + t[7] * y + t[8] * z;
                return core::Vec3{basis[0] * tx + basis[1] * ty + basis[2] * tz - object.position[0],
                                  basis[3] * tx + basis[4] * ty + basis[5] * tz + object.position[1],
                                  basis[6] * tx + basis[7] * ty + basis[8] * tz + object.position[2]} +
                       pose.offset;
            };
            // The class's model (GeometryName), if it has one, and its
            // collision, placed in game space.
            std::string model;
            std::string door_json;
            std::string destructible_json;
            std::string animation_json;
            if (library.classes().find(object.type) != nullptr) {
                const OdfClass resolved = library.classes().resolve(object.type);
                // A door's opening: its class's Animation clip from the
                // AnimationName bank, written with its model.
                std::vector<ModelClip> clips;
                if (lowercase(resolved.base) == "door") {
                    const auto bank_name = resolved.get("AnimationName");
                    const auto clip_name = resolved.get("Animation");
                    const UcfbNode* bank = bank_name ? library.animation_bank(*bank_name) : nullptr;
                    const UcfbNode* bin = bank ? bank->child("BIN_") : nullptr;
                    if (bin != nullptr && clip_name) {
                        for (Animation& clip : parse_animation_bank(bin->data)) {
                            if (clip.name_hash == name_hash(*clip_name)) {
                                clips.push_back({lowercase(*clip_name), std::move(clip)});
                                break;
                            }
                        }
                    }
                    if (clips.empty()) {
                        std::cerr << std::format("door {}: no clip {} in bank {}\n", object.name,
                                                 clip_name.value_or("?"), bank_name.value_or("?"));
                    }
                }
                if (const auto geometry = resolved.get("GeometryName")) {
                    model = model_file(*geometry, clips);
                    // By model and what the class says its soldiers and
                    // ordnance meet.
                    const CollisionUse use = collision_use(resolved);
                    std::string key = lowercase(*geometry);
                    for (const auto& names : {use.soldiers, use.ordnance}) {
                        key += '|';
                        for (const std::string& name : names.value_or(std::vector<std::string>{"*"})) {
                            key += name + ',';
                        }
                    }
                    auto shape = shapes.find(key);
                    if (shape == shapes.end()) {
                        shape = shapes.emplace(key, model_collision(library, *geometry, use)).first;
                    }
                    // Doors (class "door": Polis Massa's) open as soldiers
                    // come (core::Door); soldiers pass them, so their
                    // collision is left out, and a shut one stops shots
                    // with its box. Not a locked one (IsLocked, as Kamino's
                    // mission script sets some of its pod rooms' doors):
                    // shut, its collision the map's.
                    const bool locked = std::atof(object.get("IsLocked").c_str()) != 0;
                    const bool door = lowercase(resolved.base) == "door";
                    if (door) {
                        ++(locked ? doors_shut : doors_open);
                    }
                    if (door && !locked) {
                        // Its trigger: AnimationTrigger's hardpoint (a bone of
                        // its model) and radius; its box: the model's
                        // collision that stops ordnance (or all of it), in its
                        // own axes; both in Battlefront's coordinates, as the
                        // map file has them.
                        const auto to_world = [&](const std::array<float, 3>& p) {
                            return std::array<double, 3>{r[0] * p[0] + r[3] * p[1] + r[6] * p[2] + object.position[0],
                                                         r[1] * p[0] + r[4] * p[1] + r[7] * p[2] + object.position[1],
                                                         r[2] * p[0] + r[5] * p[1] + r[8] * p[2] + object.position[2]};
                        };
                        std::string hardpoint;
                        double radius = 3;
                        std::istringstream(resolved.get("AnimationTrigger").value_or("")) >> hardpoint >> radius;
                        std::array<float, 3> trigger_local{0, 1, 0};
                        if (const UcfbNode* skel = library.skeleton(*geometry); skel && !hardpoint.empty()) {
                            const Skeleton skeleton = read_skeleton(*skel);
                            const bool found = std::any_of(skeleton.bones.begin(), skeleton.bones.end(), [&](const ModelBone& b) {
                                return lowercase(b.name) == lowercase(hardpoint);
                            });
                            if (found) {
                                for (const ModelBone& b : skeleton.bones) {
                                    if (lowercase(b.name) == lowercase(hardpoint)) {
                                        trigger_local = skeleton.model_transform(b.name).position;
                                    }
                                }
                            } else {
                                std::cerr << std::format("door {}: no hardpoint {}\n", object.name, hardpoint);
                            }
                        }
                        std::array<float, 3> low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};
                        const bool any_ordnance = std::any_of(shape->second.begin(), shape->second.end(),
                                                              [](const CollisionTriangle& t) { return t.ordnance; });
                        for (const CollisionTriangle& t : shape->second) {
                            if (any_ordnance && !t.ordnance) {
                                continue;
                            }
                            for (const auto& p : {t.a, t.b, t.c}) {
                                for (size_t i = 0; i < 3; ++i) {
                                    low[i] = std::min(low[i], p[i]);
                                    high[i] = std::max(high[i], p[i]);
                                }
                            }
                        }
                        if (low[0] <= high[0]) {
                            const auto trigger = to_world(trigger_local);
                            const auto center = to_world({(low[0] + high[0]) / 2, (low[1] + high[1]) / 2,
                                                          (low[2] + high[2]) / 2});
                            const double open_time =
                                clips.empty() || clips[0].animation.frame_count < 2
                                    ? 1.0
                                    : double(clips[0].animation.frame_count - 1) / animation_fps;
                            std::string line = std::format(
                                "Door = {:.2f} {:.2f} {:.2f} {:g} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}",
                                trigger[0], trigger[1], trigger[2], radius, open_time, center[0], center[1], center[2],
                                std::max(0.05f, (high[0] - low[0]) / 2), std::max(0.05f, (high[1] - low[1]) / 2),
                                std::max(0.05f, (high[2] - low[2]) / 2));
                            for (int row = 0; row < 3; ++row) {
                                for (int column = 0; column < 3; ++column) {
                                    line += std::format(" {:.4f}", r[size_t(column * 3 + row)]);
                                }
                            }
                            const std::string open_sound = lowercase(first_token(resolved.get("OpenSound").value_or("")));
                            const std::string close_sound = lowercase(first_token(resolved.get("CloseSound").value_or("")));
                            door_json = std::format(
                                ", \"door\": {{\"index\": {}, \"animation\": {}, \"time\": {:.3f}, "
                                "\"open_sound\": {}, \"close_sound\": {}}}",
                                doors.size(), json_string(clips.empty() ? "" : clips[0].name), open_time,
                                json_string(open_sound), json_string(close_sound));
                            doors.push_back(line);
                        } else {
                            std::cerr << std::format("door {}: no collision for its box\n", object.name);
                        }
                    }
                    // Something that can be destroyed (a "destructablebuilding"
                    // with health): its collision is its part's, there while it
                    // stands; its wreck's (DestroyedGeometryName) there once
                    // it's destroyed. Its bounds (game space) and explosion
                    // (ExplosionName's) go in the map file.
                    // Its MaxHealth: the mission script's (--set), over its class's.
                    const std::string set_health = object.get("MaxHealth");
                    const double max_health =
                        std::atof((set_health.empty() ? resolved.get("MaxHealth").value_or("0") : set_health).c_str());
                    const bool destructible = lowercase(resolved.base) == "destructablebuilding" && max_health > 0;
                    int16_t part = destructible ? int16_t(destructibles.size() + 1) : int16_t(0);
                    // Moved by a world animation that a destroyed object
                    // plays: where it rests while that stands, where the
                    // animation leaves it once it's destroyed (its last
                    // position key; ASSUMPTION: rotation keys don't move its
                    // collision).
                    // Moved by a world animation a destroyed object plays: where
                    // its first keys have it while that stands, its last once
                    // it's destroyed.
                    Pose standing = rest;
                    std::optional<Pose> moved;
                    if (kill_moves != nullptr && !destructible) {
                        part = int16_t(core::moved_part + kill_moves->trigger + 1);
                        standing = pose_at(*kill_moves->animation, 0);
                        moved = pose_at(*kill_moves->animation, last_time(*kill_moves->animation));
                    }
                    core::Vec3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
                    const auto add = [&](const std::vector<CollisionTriangle>& triangles, int16_t as,
                                         const Pose& pose) {
                        for (const CollisionTriangle& t : triangles) {
                            core::MapCollision::Triangle placed{
                                posed(t.a, pose), posed(t.b, pose), posed(t.c, pose),
                                uint8_t((t.soldiers ? core::blocks_characters : 0) |
                                        (t.ordnance ? core::blocks_shots : 0)),
                                as};
                            if (as > 0) {
                                for (const core::Vec3& p : {placed.a, placed.b, placed.c}) {
                                    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                                    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                                }
                            }
                            collision.triangles.push_back(placed);
                        }
                    };
                    add(door && !locked ? std::vector<CollisionTriangle>{} : shape->second, part, standing);
                    if (moved) {
                        add(shape->second, int16_t(-part), *moved);
                    }
                    if (destructible) {
                        std::string wreck;
                        if (const auto destroyed = resolved.get("DestroyedGeometryName")) {
                            wreck = model_file(*destroyed);
                            if (library.model(*destroyed) != nullptr) {
                                add(model_collision(library, *destroyed, collision_use(resolved)), int16_t(-part), rest);
                            }
                        }
                        if (low.x > high.x) {
                            // No collision: a meter round where it stands.
                            const core::Vec3 at{-object.position[0], object.position[1] + 1, object.position[2]};
                            low = at - core::Vec3{1, 1, 1};
                            high = at + core::Vec3{1, 1, 1};
                        }
                        double damage = 0, inner = 0, outer = 0;
                        std::string effect;
                        std::string explosion_sound;
                        if (const auto name = resolved.get("ExplosionName")) {
                            const OdfClass explosion = library.classes().resolve(*name);
                            damage = std::atof(explosion.get("Damage").value_or("0").c_str());
                            // Its radii, or one: DamageRadius (ASSUMPTION: full
                            // at the middle, falling to nothing there).
                            inner = std::atof(explosion.get("DamageRadiusInner").value_or("0").c_str());
                            outer = std::atof(
                                explosion.get("DamageRadiusOuter").value_or(explosion.get("DamageRadius").value_or("0")).c_str());
                            effect = lowercase(explosion.get("Effect").value_or(""));
                            explosion_sound = lowercase(first_token(explosion.get("SoundProperty").value_or("")));
                        }
                        const core::Vec3 center = (low + high) * 0.5, half = (high - low) * 0.5;
                        // Its damage effects (smoke, sparks, flames at its
                        // hardpoints while its health is between a start and
                        // a stop percent: DamageStartPercent,
                        // DamageStopPercent, DamageEffect, DamageEffectScale,
                        // DamageAttachPoint, in that order per effect), the
                        // hardpoints in its own axes (game space); and the
                        // chunks it throws when destroyed (ChunkGeometryName,
                        // ChunkSpeed, ChunkUpFactor per chunk).
                        Skeleton skeleton;
                        if (const UcfbNode* skel = library.skeleton(*geometry)) {
                            skeleton = read_skeleton(*skel);
                        }
                        std::string damage_effects, chunks;
                        struct Damage {
                            double start = 100, stop = 0, scale = 1;
                            std::string effect, at;
                        };
                        std::vector<Damage> damages;
                        struct Chunk {
                            std::string model;
                            double speed = 5, up = 0;
                        };
                        std::vector<Chunk> chunk_list;
                        for (const auto& [hash, value] : resolved.properties) {
                            if (hash == odf_hash("DamageStartPercent")) {
                                damages.push_back({});
                                damages.back().start = std::atof(value.c_str());
                            } else if (!damages.empty() && hash == odf_hash("DamageStopPercent")) {
                                damages.back().stop = std::atof(value.c_str());
                            } else if (!damages.empty() && hash == odf_hash("DamageEffect")) {
                                damages.back().effect = lowercase(value);
                            } else if (!damages.empty() && hash == odf_hash("DamageEffectScale")) {
                                damages.back().scale = std::atof(value.c_str());
                            } else if (!damages.empty() && hash == odf_hash("DamageAttachPoint")) {
                                damages.back().at = value;
                            } else if (hash == odf_hash("ChunkGeometryName")) {
                                chunk_list.push_back({model_file(value), 5, 0});
                            } else if (!chunk_list.empty() && hash == odf_hash("ChunkSpeed")) {
                                chunk_list.back().speed = std::atof(value.c_str());
                            } else if (!chunk_list.empty() && hash == odf_hash("ChunkUpFactor")) {
                                chunk_list.back().up = std::atof(value.c_str());
                            }
                        }
                        for (const Damage& d : damages) {
                            if (d.effect.empty()) {
                                continue;
                            }
                            std::string bone = d.at;  // its hardpoint, by name whatever the case
                            for (const ModelBone& b : skeleton.bones) {
                                if (lowercase(b.name) == lowercase(d.at)) {
                                    bone = b.name;
                                }
                            }
                            const auto p = skeleton.model_transform(bone).position;  // (origin if no such bone)
                            damage_effects += std::format(
                                "{}{{\"effect\": {}, \"start\": {:g}, \"stop\": {:g}, \"scale\": {:g}, "
                                "\"at\": [{:g}, {:g}, {:g}]}}",
                                damage_effects.empty() ? "" : ", ", json_string(d.effect), d.start, d.stop, d.scale,
                                -p[0], p[1], p[2]);
                        }
                        for (const Chunk& c : chunk_list) {
                            if (!c.model.empty()) {
                                chunks += std::format("{}{{\"model\": {}, \"speed\": {:g}, \"up\": {:g}}}",
                                                      chunks.empty() ? "" : ", ", json_string(c.model), c.speed, c.up);
                            }
                        }
                        destructible_json = std::format(
                            ", \"destructible\": {{\"index\": {}, \"health\": {:g}, \"destroyed_model\": {}, "
                            "\"explosion_effect\": {}, \"explosion_sound\": {}, \"explosion_radius\": {:g}, "
                            "\"center\": [{:.2f}, {:.2f}, {:.2f}], \"damage_effects\": [{}], \"chunks\": [{}]}}",
                            destructibles.size(), max_health, json_string(wreck), json_string(effect),
                            json_string(explosion_sound), outer, center.x, center.y, center.z, damage_effects,
                            chunks);
                        const auto regen = regens.find(lowercase(object.name));
                        destructibles.push_back(std::format(
                            "Destructible = {:g} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:g} {:g} {:g} {:g} {}",
                            max_health, -center.x, center.y, center.z, std::max(0.05, half.x), std::max(0.05, half.y),
                            std::max(0.05, half.z), damage, inner, outer,
                            regen == regens.end() ? 0.0 : regen->second, object.name));
                    }
                }
            }
            // The world animations moving it: their keys in game space
            // (positions x mirrored; rotations about x, y, z with y and z
            // turned the other way), whether they play from the start, and
            // the destructible that plays or stops them.
            if (const auto it = object_animations.find(lowercase(object.name)); it != object_animations.end()) {
                animation_json = ", \"animations\": [";
                size_t listed = 0;
                for (size_t i = 0; i < it->second.size(); ++i) {
                    const ObjectAnimation& a = it->second[i];
                    if (a.mode == "start") {
                        continue;  // played at the start: it stands where that leaves it
                    }
                    const auto keys = [](const std::vector<WorldAnimation::Key>& list, float sign_x, float sign_yz) {
                        std::string out = "[";
                        for (size_t k = 0; k < list.size(); ++k) {
                            out += std::format("{}[{:g}, {:g}, {:g}, {:g}]", k ? ", " : "", list[k].time,
                                               sign_x * list[k].value[0], sign_yz * list[k].value[1],
                                               sign_yz * list[k].value[2]);
                        }
                        return out + "]";
                    };
                    float length = a.animation->length;
                    for (const auto* list : {&a.animation->positions, &a.animation->rotations}) {
                        for (const auto& key : *list) {
                            length = std::max(length, key.time);
                        }
                    }
                    // Positions: x mirrored. Rotations: y and z the other way.
                    std::string positions = "[";
                    for (size_t k = 0; k < a.animation->positions.size(); ++k) {
                        const auto& key = a.animation->positions[k];
                        positions += std::format("{}[{:g}, {:g}, {:g}, {:g}]", k ? ", " : "", key.time, -key.value[0],
                                                 key.value[1], key.value[2]);
                    }
                    positions += "]";
                    animation_json += std::format(
                        "{}{{\"group\": {}, \"start\": {}, \"loop\": {}, \"local\": {}, \"length\": {:g}, "
                        "\"positions\": {}, \"rotations\": {}, \"destructible\": {}, \"mode\": {}}}",
                        listed++ ? ", " : "", json_string(a.group->name), a.group->play_at_start ? "true" : "false",
                        a.animation->loop ? "true" : "false", a.animation->local ? "true" : "false", length, positions,
                        keys(a.animation->rotations, 1, -1), a.trigger, json_string(a.mode));
                }
                animation_json += "]";
            }
            // A command post's hologram: its owner's insignia floating over it
            // (HoloOdf, HoloImageGeometry per side), and its minimap icon.
            std::string hologram;
            if (library.classes().find(object.type) != nullptr) {
                const OdfClass resolved = library.classes().resolve(object.type);
                if (const auto holo_odf = resolved.get("HoloOdf")) {
                    const OdfClass holo = library.classes().resolve(*holo_odf);
                    const auto number = [&](std::string_view name, double fallback) {
                        const auto value = holo.get(name);
                        return value ? std::atof(value->c_str()) : fallback;
                    };
                    const auto color = [&](std::string_view name) {
                        const std::string value = holo.get(name).value_or("255 255 255");
                        int r = 255, g = 255, b = 255;
                        std::istringstream(value) >> r >> g >> b;
                        return std::format("[{:.3f}, {:.3f}, {:.3f}]", r / 255.0, g / 255.0, b / 255.0);
                    };
                    std::string images;
                    for (const auto& [hash, value] : resolved.properties) {
                        if (hash != odf_hash("HoloImageGeometry")) {
                            continue;
                        }
                        const size_t space = value.find(' ');
                        if (space == std::string::npos) {
                            continue;
                        }
                        const std::string file = model_file(value.substr(0, space));
                        if (!file.empty()) {
                            images += (images.empty() ? "" : ", ") + json_string(lowercase(value.substr(space + 1))) +
                                      ": " + json_string(file);
                        }
                    }
                    hologram = std::format(
                        ", \"hologram\": {{\"images\": {{{}}}, \"height\": {}, \"size\": {}, \"rotate_rate\": {}, "
                        "\"light_radius\": {}, \"friendly_color\": {}, \"enemy_color\": {}, \"neutral_color\": {}, "
                        "\"map_texture\": {}, \"map_scale\": {}}}",
                        images, number("HoloHeight", 3.5), number("HoloSize", 2.5), number("HoloRotateRate", 0.2),
                        number("HoloLightRadius", 8.0), color("FriendlyColor"), color("EnemyColor"),
                        color("NeutralColor"), json_string(texture_file(resolved.get("MapTexture").value_or(""))),
                        std::atof(resolved.get("MapScale").value_or("1").c_str()));
                }
            }
            // A health or ammo droid ("powerupstation"): what it gives, how
            // often (PowerupDelay, SoldierHealth, SoldierAmmo, Radius if the
            // class sets one) for the map file, and how it idles for drawing
            // (IdleRotateSpeed, IdleWaitTime: the health droid turns and
            // pauses; IdleWobbleFactor: the GONK rocks) and its map icon.
            std::string powerup;
            if (library.classes().find(object.type) != nullptr) {
                const OdfClass resolved = library.classes().resolve(object.type);
                if (lowercase(resolved.base) == "powerupstation") {
                    const auto number = [&](std::string_view name, double fallback) {
                        const auto value = resolved.get(name);
                        return value ? std::atof(value->c_str()) : fallback;
                    };
                    const double radius = number("Radius", core::PowerupStation::default_radius);
                    const double delay = number("PowerupDelay", 1);
                    const double health = number("SoldierHealth", 0);
                    const double ammo = number("SoldierAmmo", 0);
                    if (delay > 0 && (health > 0 || ammo > 0)) {
                        powerups.push_back(std::format("Powerup = {:.2f} {:.2f} {:.2f} {:g} {:g} {:g} {:g}",
                                                       object.position[0], object.position[1], object.position[2],
                                                       radius, delay, health, ammo));
                        powerup = std::format(
                            ", \"powerup\": {{\"kind\": {}, \"radius\": {:g}, \"delay\": {:g}, \"health\": {:g}, "
                            "\"ammo\": {:g}, \"idle_rotate_speed\": {:g}, \"idle_wait_time\": {:g}, "
                            "\"wobble\": {:g}, \"map_texture\": {}}}",
                            json_string(health > 0 ? "health" : "ammo"), radius, delay, health, ammo,
                            number("IdleRotateSpeed", 0), number("IdleWaitTime", 0), number("IdleWobbleFactor", 0),
                            json_string(lowercase(resolved.get("MapTexture").value_or(""))));
                    }
                }
            }
            json << (written++ ? ",\n" : "\n") << "    {\"layer\": " << json_string(object.layer)
                 << ", \"class\": " << json_string(object.type) << ", \"name\": " << json_string(object.name)
                 << ", \"model\": " << json_string(model) << hologram << powerup << door_json << destructible_json << animation_json
                 << std::format(", \"position\": [{}, {}, {}], \"basis\": [", rest_position.x, rest_position.y,
                                rest_position.z);
            for (size_t i = 0; i < 9; ++i) {
                json << (i ? ", " : "") << float(rest_basis[i]);
            }
            json << "]}";
        }
        json << "\n  ]\n}\n";
        std::cerr << std::format("objects: {} of {} placed ({} left open); {} models, {} textures\n", written,
                                 level.objects.size(), left_open, models.size(), textures.size());
        core::save_map_collision(collision, core::map_collision_path(out_dir / map));
        std::cerr << std::format("collision: {} triangles; doors: {} open, {} locked shut\n",
                                 collision.triangles.size(), doors_open, doors_shut);
    }

    // The mode's command posts: capture regions, starting teams, spawn points.
    std::ofstream ini(out_dir / (map + "_" + mode + ".ini"), std::ios::binary);
    const double edge_x = terrain.origin_x(), edge_z = terrain.origin_z();
    const double extent = terrain.cells * terrain.unit;
    ini << "; " << map << "'s " << mode << " (" << mode_layers
        << "), written by bf2tool export-map from your own Battlefront II install.\n"
           "; Game data: don't commit or share it. Positions are Battlefront coordinates (x z).\n\n[map]\nName = "
        << map << "\nTerrain = " << map << "\n"
        << std::format("Bounds = {} {} {} {}\n", edge_x, edge_z, edge_x + extent, edge_z + extent);
    // The AI barriers that stop soldiers: places bots keep out of when they can.
    size_t barriers = 0;
    for (const LevelBarrier& barrier : level.barriers) {
        if (!included(barrier) || !(barrier.flags & 1) ||
            std::find(disabled.begin(), disabled.end(), lowercase(barrier.name)) != disabled.end()) {
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
    if (std::getenv("BF2_BARRIERS")) {
        for (const LevelBarrier& barrier : level.barriers) {
            std::cerr << std::format("  barrier {} ({}, flags {}) at {:.0f} {:.0f}, {:.0f} x {:.0f}\n", barrier.name,
                                     barrier.layer, barrier.flags, barrier.position[0], barrier.position[2],
                                     barrier.half_size[0] * 2, barrier.half_size[1] * 2);
        }
    }
    // The playable area: the paths the map's boundaries list (Felucia's are
    // four overlapping rings, "warning1".."Warning4"), or, in a map listing
    // none, the paths named "boundary..." (Polis Massa's "Boundry1"). Each
    // once: a sublevel may repeat its mode's copy.
    const auto is_boundary = [&](const LevelPath& path) {
        if (level.boundary_paths.empty()) {
            const std::string name = lowercase(path.name);
            return name.starts_with("boundary") || name.starts_with("boundry");
        }
        const uint32_t hash = odf_hash(path.name);
        return std::any_of(level.boundary_paths.begin(), level.boundary_paths.end(), [&](const auto& listed) {
            return listed.second == hash && (listed.first == path.group || path_groups.contains(listed.first));
        });
    };
    std::vector<std::vector<PathNode>> boundaries;
    for (const LevelPath& path : level.paths) {
        if (!path_groups.contains(path.group) || path.nodes.size() < 3 || !is_boundary(path)) {
            continue;
        }
        if (std::find(dropped_boundaries.begin(), dropped_boundaries.end(), lowercase(path.name)) !=
            dropped_boundaries.end()) {
            std::cerr << std::format("boundary: {} left out\n", path.name);
            continue;
        }
        const bool repeat = std::any_of(boundaries.begin(), boundaries.end(), [&](const auto& nodes) {
            return nodes.size() == path.nodes.size() &&
                   std::equal(nodes.begin(), nodes.end(), path.nodes.begin(),
                              [](const PathNode& a, const PathNode& b) { return a.position == b.position; });
        });
        if (!repeat) {
            boundaries.push_back(path.nodes);
            std::cerr << std::format("boundary: {} ({} points)\n", path.name, path.nodes.size());
            ini << "Boundary =";
            for (const PathNode& node : path.nodes) {
                ini << std::format(" {:.2f} {:.2f}", node.position[0], node.position[2]);
            }
            ini << "\n";
        }
    }
    // Death regions: shape, center (x y z), size, and rotation (rows), as
    // Battlefront stores them.
    size_t death_regions = 0;
    for (const WorldItem& region : level.regions) {
        if (!included(region)) {
            continue;
        }
        const bool named = std::any_of(deaths.begin(), deaths.end(), [&](const std::string& death) {
            return lowercase(region.name) == lowercase(death) || lowercase(region.get("Name")) == lowercase(death);
        });
        if (!named) {
            continue;
        }
        const auto& r = region.rotation;  // stored columns
        ini << std::format("DeathRegion = {} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}", region.type, region.position[0],
                           region.position[1], region.position[2], region.size[0], region.size[1], region.size[2]);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                ini << std::format(" {:.4f}", r[size_t(column * 3 + row)]);
            }
        }
        // Only while the object whose destruction removes it stands; on a
        // timer.
        for (const auto& [object, name] : kill_removes) {
            if ((name == lowercase(region.name) || name == lowercase(region.get("Name"))) &&
                destructible_index.contains(object)) {
                ini << " standing " << destructible_index[object];
            }
        }
        for (const auto& [name, timing] : timed_deaths) {
            if (name == lowercase(region.name) || name == lowercase(region.get("Name"))) {
                ini << std::format(" timed {:g} {:g} {:g}", timing[0], timing[1], timing[2]);
            }
        }
        ini << "\n";
        ++death_regions;
    }
    // Damage regions: the map's own, always on (no script adds them): a
    // region whose class is "damageregion damagerate=<n> personscale=<n>
    // ...", hurting soldiers in it by damagerate times personscale a second
    // (the scales default to 1). Mustafar's lava, some 1000 a second; the
    // heat near it, 50.
    size_t damage_regions = 0;
    for (const WorldItem& region : level.regions) {
        std::istringstream words(lowercase(region.name));
        std::string word;
        if (!included(region) || !(words >> word) || word != "damageregion") {
            continue;
        }
        double rate = 0, scale = 1;
        while (words >> word) {
            const size_t equals = word.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string key = word.substr(0, equals);
            const double value = std::atof(word.c_str() + equals + 1);
            if (key == "damagerate") {
                rate = value;
            } else if (key == "personscale") {
                scale = value;
            }
        }
        if (rate * scale <= 0) {
            continue;
        }
        const auto& r = region.rotation;  // stored columns
        ini << std::format("DamageRegion = {:g} {} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}", rate * scale, region.type,
                           region.position[0], region.position[1], region.position[2], region.size[0], region.size[1],
                           region.size[2]);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                ini << std::format(" {:.4f}", r[size_t(column * 3 + row)]);
            }
        }
        ini << "\n";
        ++damage_regions;
    }
    for (const std::string& line : powerups) {
        ini << line << "\n";
    }
    for (const std::string& line : doors) {
        ini << line << "\n";
    }
    for (const std::string& line : destructibles) {
        ini << line << "\n";
    }
    if (no_terrain) {
        // Off the ship: falling to the stand-in ground, through this.
        const double half = terrain.cells * terrain.unit / 2;
        ini << std::format("DeathRegion = box 0 {:.2f} 0 {:.2f} 20 {:.2f} 1 0 0 0 1 0 0 0 1\n", fall_y + 20, half,
                           half);
        ++death_regions;
    }
    // Deep water: soldiers can't swim, so whoever is more than drown_depth
    // under its surface dies, as in Battlefront (ASSUMPTION: its soldier
    // class has nothing on water but splashes, and its maps keep soldiers
    // out of deep water or kill them there; Yavin has a spawn point on its
    // riverbed 2 m under, so it's deeper than that). Shallower is waded:
    // Yavin's river, Kashyyyk's shore, Dagobah's swamp, Jabba's rancor
    // pit's pool. Deeper dies: Kamino's sea, Naboo's canals (3.5 m),
    // Felucia's pond (8.8).
    constexpr double drown_depth = 2.5;
    double bottom = fall_y;
    if (!no_terrain) {
        bottom = *std::min_element(level.terrain.heights.begin(), level.terrain.heights.end()) - 10;
    }
    for (const auto& [x0, z0, x1, z1, height] : water_boxes) {
        const double top = height - drown_depth;
        if (top > bottom) {
            ini << std::format("DeathRegion = box {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} {:.2f} 1 0 0 0 1 0 0 0 1\n",
                               (x0 + x1) / 2, (top + bottom) / 2, (z0 + z1) / 2, (x1 - x0) / 2, (top - bottom) / 2,
                               (z1 - z0) / 2);
            ++death_regions;
        }
    }
    std::cerr << std::format("boundaries: {}; death regions: {} ({} named); damage regions: {}; droids: {}\n",
                             boundaries.size(), death_regions, deaths.size(), damage_regions, powerups.size());
    int posts = 0;
    for (const WorldItem& object : level.objects) {
        if (object.sublevel != mode_sublevel || object.get("CaptureRegion").empty() ||
            std::find(opened.begin(), opened.end(), lowercase(object.name)) != opened.end()) {
            continue;
        }
        const WorldItem* capture = level.region(object.get("CaptureRegion"), mode_sublevel);
        const auto& at = capture ? capture->position : object.position;
        ini << "\n[post]\nName = " << (object.get("Label").empty() ? object.name : object.get("Label")) << "\n"
            << std::format("Position = {:.2f} {:.2f}\n", at[0], at[2])
            // The post object itself (its console), on the floor the post
            // is fought over.
            << std::format("Console = {:.2f} {:.2f} {:.2f}\n", object.position[0], object.position[2],
                           object.position[1]);
        if (capture != nullptr) {
            // ASSUMPTION: a capture cylinder's SIZE x is its radius.
            ini << std::format("Radius = {:.2f}\n", std::max(capture->size[0], capture->size[2]));
            // How high it reaches (a sphere's radius, else half its y
            // size), and the post itself: a bunker's post isn't taken from
            // the hill over it.
            const double half = capture->type == "sphere" ? capture->size[0] : capture->size[1];
            const double post_y = object.position[1];
            ini << std::format("Altitude = {:.2f} {:.2f}\n", std::min(capture->position[1] - half, post_y - 0.5),
                               std::max(capture->position[1] + half, post_y + 2.0));
        }
        std::string team = object.get("Team");
        if (const auto set = teams.find(lowercase(object.name)); set != teams.end()) {
            team = set->second;
        }
        if (team == "1" || team == "2") {
            ini << "Owner = " << team << "\n";
        }
        // Where natives friendly to its holder spawn (its AllyPath): heights
        // given.
        if (const LevelPath* allies = level.path_in(object.get("AllyPath"), mode_sublevel)) {
            for (const PathNode& node : allies->nodes) {
                ini << std::format("AllySpawnPoint = {:.2f} {:.2f} {:.2f}\n", node.position[0], node.position[2],
                                   node.position[1]);
            }
        }
        if (const LevelPath* spawns = level.path_in(object.get("SpawnPath"), mode_sublevel)) {
            for (const PathNode& node : spawns->nodes) {
                ini << std::format("SpawnPoint = {:.2f} {:.2f} {:.2f}\n", node.position[0], node.position[2],
                                   node.position[1]);
            }
        }
        ++posts;
    }
    std::cerr << std::format("{} ({}): {} command posts\nwrote {}\n", mode, mode_layers, posts, out_dir.string());
    if (posts == 0) {
        throw std::runtime_error("no command posts in " + mode + " (" + mode_layers + ")");
    }
    return 0;
}

void write_hud_node(std::ostream& out, const HudNode& node, int depth) {
    const std::string indent(size_t(depth) * 2, ' ');
    out << indent << "{\"keyword\": " << json_string(node.keyword);
    if (node.has_text) {
        out << ", \"text\": " << json_string(node.text);
    }
    if (!node.values.empty()) {
        out << ", \"values\": [";
        for (size_t i = 0; i < node.values.size(); ++i) {
            out << (i ? ", " : "");
            if (node.values[i].is_text) {
                out << json_string(node.values[i].text);
            } else {
                out << std::format("{:.6g}", node.values[i].number);
            }
        }
        out << "]";
    }
    if (!node.children.empty()) {
        out << ", \"children\": [\n";
        for (size_t i = 0; i < node.children.size(); ++i) {
            write_hud_node(out, node.children[i], depth + 1);
            out << (i + 1 < node.children.size() ? ",\n" : "\n");
        }
        out << indent << "]";
    }
    out << "}";
}

void collect_strings(const HudNode& node, std::set<std::string>& out) {
    if (node.has_text && !node.text.empty()) {
        out.insert(lowercase(node.text));
    }
    for (const HudNode& child : node.children) {
        collect_strings(child, out);
    }
}

// Writes Battlefront II's HUD for the game's UI: its layouts (hud.json:
// "layouts", each a tree of elements and properties), its fonts ("fonts":
// glyph metrics; pages as PNG), and the textures and models the layouts name
// (textures/, models/ as glTF), plus every texture in ingame.lvl and
// common.lvl (icons, markers, the menus' buttons and borders), and its English
// strings.
int cmd_export_hud(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path lvl_dir = args[0];
    const std::filesystem::path out_dir = option(args, "-o");
    if (out_dir.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path texture_dir = out_dir / "textures";
    const std::filesystem::path model_dir = out_dir / "models";
    std::filesystem::create_directories(texture_dir);
    std::filesystem::create_directories(model_dir);

    AssetLibrary library;
    std::vector<std::vector<uint8_t>> bytes;
    std::vector<UcfbNode> roots;
    bytes.reserve(3);
    roots.reserve(3);
    for (const char* file : {"ingame.lvl", "common.lvl", "core.lvl"}) {
        bytes.push_back(read_file(lvl_dir / file));
        library.add(bytes.back());
        roots.push_back(parse_ucfb(bytes.back()));
    }

    // Layouts.
    std::vector<std::pair<std::string, std::vector<HudNode>>> layouts;
    std::set<std::string> strings;
    visit_ucfb(roots[0], [&](const UcfbNode& node, int) {
        if (node.tag != "hud_") {
            return;
        }
        std::vector<HudNode> tree = read_config_tree(node);
        if (tree.empty()) {
            return;
        }
        for (const HudNode& element : tree) {
            collect_strings(element, strings);
        }
        layouts.emplace_back(tree.front().text, std::move(tree));
    });

    // Textures and models the layouts name; dropped pickup models from
    // ingame.lvl (class "powerupitem"); every texture in ingame.lvl and
    // common.lvl.
    std::set<std::string> textures;
    std::set<std::string> exported_models;
    std::vector<std::string> models;
    const auto maybe_export_model = [&](const std::string& name) {
        const std::string key = lowercase(name);
        if (key.empty() || exported_models.contains(key) || library.model(key) == nullptr) {
            return;
        }
        const ModelExport written = export_model(library, key, model_dir, texture_dir, textures);
        if (written.segments > 0) {
            exported_models.insert(key);
            models.push_back(key);
        }
    };
    for (const std::string& name : strings) {
        if (library.texture(name) != nullptr && export_texture(library, name, texture_dir / (name + ".png"))) {
            textures.insert(name);
        }
        maybe_export_model(name);
    }
    for (const OdfClass& odf : library.classes().all()) {
        const OdfClass resolved = library.classes().resolve(odf.type);
        if (lowercase(resolved.base) == "powerupitem") {
            if (const auto geom = resolved.get("GeometryName")) {
                maybe_export_model(*geom);
            }
        }
    }
    // Every texture of ingame.lvl (icons, markers, the stats screens') and
    // common.lvl (the menus' art).
    for (const UcfbNode* root : {&roots[0], &roots[1]}) {
        for (const UcfbNode& child : root->children) {
            if (child.tag == "tex_") {
                const std::string name = lowercase(child.name());
                if (!textures.contains(name) && export_texture(library, name, texture_dir / (name + ".png"))) {
                    textures.insert(name);
                }
            }
        }
    }

    // Fonts.
    std::vector<Font> fonts;
    visit_ucfb(roots[2], [&](const UcfbNode& node, int) {
        if (node.tag == "font") {
            fonts.push_back(read_font(node));
        }
    });

    // English strings (core.lvl's "english" Locl), by key hash.
    std::vector<std::pair<uint32_t, std::string>> english;
    visit_ucfb(roots[2], [&](const UcfbNode& node, int) {
        if (node.tag == "Locl" && node.name() == "english") {
            english = read_strings(node);
        }
    });

    std::ofstream out(out_dir / "hud.json");
    out << "{\n\"layouts\": {\n";
    for (size_t i = 0; i < layouts.size(); ++i) {
        out << json_string(layouts[i].first) << ": [\n";
        for (size_t j = 0; j < layouts[i].second.size(); ++j) {
            write_hud_node(out, layouts[i].second[j], 1);
            out << (j + 1 < layouts[i].second.size() ? ",\n" : "\n");
        }
        out << "]" << (i + 1 < layouts.size() ? ",\n" : "\n");
    }
    out << "},\n\"fonts\": {\n";
    for (size_t i = 0; i < fonts.size(); ++i) {
        const Font& font = fonts[i];
        out << json_string(font.name) << ": {\"line_height\": " << font.line_height << ", \"pages\": [";
        for (size_t p = 0; p < font.pages.size(); ++p) {
            const std::string page = lowercase(font.pages[p]->name());
            if (const auto image = read_texture(*font.pages[p])) {
                swg::write_png(texture_dir / (page + ".png"), *image);
            }
            out << (p ? ", " : "") << json_string(page);
        }
        out << "],\n  \"glyphs\": [";
        for (size_t g = 0; g < font.glyphs.size(); ++g) {
            const FontGlyph& glyph = font.glyphs[g];
            out << (g ? ",\n    " : "\n    ")
                << std::format("[{}, {}, {}, {}, {}, {}, {}, {:.6g}, {:.6g}, {:.6g}, {:.6g}]", glyph.character,
                               glyph.page, glyph.advance, glyph.x_offset, glyph.width, glyph.top,
                               glyph.bottom, glyph.uv[0], glyph.uv[1], glyph.uv[2], glyph.uv[3]);
        }
        out << "]}" << (i + 1 < fonts.size() ? ",\n" : "\n");
    }
    out << "},\n\"glyph_fields\": [\"character\", \"page\", \"advance\", \"x_offset\", \"width\", "
           "\"top\", \"bottom\", \"u0\", \"u1\", \"v0\", \"v1\"],\n\"textures\": [";
    size_t n = 0;
    for (const std::string& name : textures) {
        out << (n++ ? ", " : "") << json_string(name);
    }
    out << "],\n\"models\": [";
    for (size_t i = 0; i < models.size(); ++i) {
        out << (i ? ", " : "") << json_string(models[i]);
    }
    out << "],\n\"strings\": {";
    for (size_t i = 0; i < english.size(); ++i) {
        out << (i ? ",\n  " : "\n  ") << std::format("\"{:08x}\": ", english[i].first)
            << json_string(english[i].second);
    }
    out << "\n}\n}\n";
    std::cout << layouts.size() << " layouts, " << fonts.size() << " fonts, " << textures.size() << " textures, "
              << models.size() << " models, " << english.size() << " strings\n";
    return 0;
}

// Writes Battlefront II's projectile & weapon effects (<name>.json), their
// particle and laser textures (textures/), and shared ordnance & muzzle flash
// models (models/) from ingame.lvl and side/*.lvl.
int cmd_export_effects(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path lvl_dir = args[0];
    const std::filesystem::path out_dir = option(args, "-o");
    if (out_dir.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path texture_dir = out_dir / "textures";
    const std::filesystem::path model_dir = out_dir / "models";
    std::filesystem::create_directories(texture_dir);
    std::filesystem::create_directories(model_dir);

    std::vector<std::filesystem::path> lvl_files{lvl_dir / "ingame.lvl"};
    for (const char* side : {"imp", "all", "rep", "cis"}) {
        if (const auto p = lvl_dir / "side" / (std::string(side) + ".lvl"); std::filesystem::exists(p)) {
            lvl_files.push_back(p);
        }
    }
    // --map <map>: a map's level too, for what its destructibles name
    // (their damage smoke and sparks, their chunks' trails, their explosions).
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--map") {
            const std::string map = args[i + 1];
            if (const auto p = lvl_dir / map.substr(0, 3) / (map + ".lvl"); std::filesystem::exists(p)) {
                lvl_files.push_back(p);
            } else {
                std::cerr << "no level " << p.string() << "\n";
            }
        }
    }
    AssetLibrary library;
    std::vector<std::vector<uint8_t>> bytes;
    std::vector<UcfbNode> roots;
    bytes.reserve(lvl_files.size());
    roots.reserve(lvl_files.size());
    for (const auto& file : lvl_files) {
        bytes.push_back(read_file(file));
        library.add(bytes.back());
        roots.push_back(parse_ucfb(bytes.back()));
    }

    // Index all fx__ chunks by their 4-byte NAME hash (c | 0x20 FNV-1a).
    std::map<uint32_t, const UcfbNode*> fx_by_hash;
    for (const UcfbNode& root : roots) {
        visit_ucfb(root, [&](const UcfbNode& node, int) {
            if (node.tag != "fx__") {
                return;
            }
            if (const UcfbNode* name = node.child("NAME"); name && name->data.size() == 4) {
                uint32_t hash = 0;
                std::memcpy(&hash, name->data.data(), 4);
                fx_by_hash.emplace(hash, &node);
            }
        });
    }

    // Collect every effect, laser texture, and ordnance model referenced by
    // any weapon, ordnance, or explosion ODF, plus shared muzzle/explosion FX.
    std::set<std::string> effect_names{"com_sfx_ord_exp", "com_sfx_explosion_lg", "com_sfx_explosion_sm",
                                       "com_sfx_weap_flame_exp_md", "com_sfx_weap_flame_exp_sm",
                                       "com_sfx_watersplash_ord", "com_sfx_watersplash_lg"};
    std::set<std::string> laser_textures{"com_sfx_laser_red", "com_sfx_laser_blue", "com_sfx_laser_green",
                                         "com_sfx_laser_yellow", "com_sfx_laser_orange", "com_sfx_laser_purple",
                                         "com_sfx_meshmuzzleflash", "com_sfx_smoke3"};
    std::set<std::string> model_names{"com_weap_inf_grenadethermal_ord", "com_weap_missile", "com_sfx_muzzleflash",
                                      "com_item_healthrecharge_dst", "com_item_weaponrecharge_dst"};

    for (const OdfClass& odf : library.classes().all()) {
        if (odf.kind == "entc") {
            // What can be destroyed: its damage effects and chunks' trails.
            const OdfClass resolved = library.classes().resolve(odf.type);
            if (lowercase(resolved.base) == "destructablebuilding") {
                for (const auto& [hash, value] : resolved.properties) {
                    if ((hash == odf_hash("DamageEffect") || hash == odf_hash("ChunkTrailEffect")) && !value.empty()) {
                        effect_names.insert(lowercase(value));
                    }
                }
            }
            continue;
        }
        if (odf.kind != "wpnc" && odf.kind != "ordc" && odf.kind != "expc") {
            continue;
        }
        const OdfClass resolved = library.classes().resolve(odf.type);
        for (const char* key : {"TrailEffect", "ImpactEffectSoft", "ImpactEffectTerrain", "ImpactEffectRigid",
                                "ImpactEffectStatic", "ImpactEffectWater", "ImpactEffect", "Effect", "WaterEffect"}) {
            if (const auto v = resolved.get(key); v && !v->empty()) {
                effect_names.insert(lowercase(*v));
            }
        }
        if (const auto tex = resolved.get("LaserTexture"); tex && !tex->empty()) {
            laser_textures.insert(lowercase(*tex));
        }
        if (odf.kind == "ordc") {
            if (const auto geom = resolved.get("GeometryName"); geom && !geom->empty()) {
                model_names.insert(lowercase(*geom));
            }
        }
    }

    std::set<std::string> textures;
    for (const std::string& tex : laser_textures) {
        if (library.texture(tex) != nullptr && export_texture(library, tex, texture_dir / (tex + ".png"))) {
            textures.insert(tex);
        }
    }

    size_t exported_effects = 0;
    for (const std::string& name : effect_names) {
        const auto it = fx_by_hash.find(effect_name_hash(name));
        if (it == fx_by_hash.end()) {
            continue;
        }
        const std::vector<EffectEmitter> emitters = read_effect(*it->second);
        if (emitters.empty()) {
            continue;
        }
        for (const EffectEmitter& e : emitters) {
            if (!e.texture.empty() && !textures.contains(e.texture) && library.texture(e.texture) != nullptr &&
                export_texture(library, e.texture, texture_dir / (e.texture + ".png"))) {
                textures.insert(e.texture);
            }
        }
        std::ofstream out(out_dir / (name + ".json"), std::ios::binary);
        write_effect_json(out, emitters);
        ++exported_effects;
    }

    size_t exported_models = 0;
    for (const std::string& name : model_names) {
        if (library.model(name) == nullptr) {
            continue;
        }
        const ModelExport written = export_model(library, name, model_dir, texture_dir, textures);
        if (written.segments > 0) {
            ++exported_models;
        }
    }

    std::cout << exported_effects << " effects, " << textures.size() << " textures, " << exported_models << " models\n";
    return 0;
}

// Writes Battlefront II's sound effects, voice-overs, and music as WAV files
// plus `sounds.json` from `sound/` banks and streams.
int cmd_export_audio(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    const std::filesystem::path lvl_dir = args[0];
    const std::filesystem::path out_dir = option(args, "-o");
    if (out_dir.empty()) {
        std::cerr << usage;
        return 2;
    }
    std::filesystem::create_directories(out_dir / "sfx");
    std::filesystem::create_directories(out_dir / "vo");
    std::filesystem::create_directories(out_dir / "music");

    SoundLibrary library;
    const std::filesystem::path sound_dir = lvl_dir / "sound";
    for (const char* priority : {"common.bnk", "global.lvl", "shell.lvl"}) {
        if (const auto p = sound_dir / priority; std::filesystem::exists(p)) {
            library.add_file(p);
        }
    }
    if (std::filesystem::exists(sound_dir)) {
        std::vector<std::filesystem::path> extra;
        for (const auto& entry : std::filesystem::directory_iterator(sound_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".lvl") {
                continue;
            }
            const std::string name = lowercase(entry.path().filename().string());
            if (name == "global.lvl" || name == "shell.lvl") {
                continue;
            }
            extra.push_back(entry.path());
        }
        std::sort(extra.begin(), extra.end());
        for (const auto& p : extra) {
            library.add_file(p);
        }
    }

    // Collect sound names referenced by ODFs in ingame.lvl and side/*.lvl.
    std::set<std::string> names;
    {
        OdfLibrary odfs;
        std::vector<std::filesystem::path> lvl_files{lvl_dir / "ingame.lvl"};
        for (const char* side : {"imp", "all", "rep", "cis"}) {
            if (const auto p = lvl_dir / "side" / (std::string(side) + ".lvl"); std::filesystem::exists(p)) {
                lvl_files.push_back(p);
            }
        }
        for (const auto& file : lvl_files) {
            if (std::filesystem::exists(file)) {
                const std::vector<uint8_t> bytes = read_file(file);
                odfs.add(parse_ucfb(bytes));
            }
        }
        for (const OdfClass& odf : odfs.all()) {
            const OdfClass resolved = odfs.resolve(odf.type);
            for (const char* key : {"FireSound", "FireEmptySound", "ReloadSound", "OverheatSound", "OverheatStopSound",
                                    "WeaponChangeSound", "ChangeModeSound", "FireLoopSound", "OrdnanceSound",
                                    "CollisionOtherSound", "SoundProperty", "PowerupSound", "HurtSound", "DeathSound",
                                    "OpenSound", "CloseSound"}) {
                if (const auto v = resolved.get(key)) {
                    const std::string tok = lowercase(first_token(*v));
                    if (!tok.empty()) {
                        names.insert(tok);
                    }
                }
            }
        }
    }

    // Core gameplay, impacts, doors, pickups, command posts, and UI sounds.
    for (const char* core_name : {
             "imp_weap_ord_exp", "imp_weap_ord_exp_lg", "com_weap_throw", "com_weap_inf_grenade_bounce",
             "com_weap_inf_ammo_empty", "com_weap_powerup_pickup", "com_weap_ammo_pickup", "health_recharge",
             "com_item_gonkrecharge", "com_weap_obj_small_exp", "com_weap_obj_med_exp", "com_weap_obj_large_exp",
             "com_weap_obj_huge_exp", "com_blg_commandpost_capture", "com_blg_commandpost_lost",
             "com_blg_commandpost_charge", "com_blg_commandpost_discharge", "com_blg_commandpost_dispute",
             "door_open", "door_close", "big_door_open", "big_door_close", "blast_door_open", "blast_door_close",
             "shell_select_unit", "shell_menu_enter", "shell_menu_exit", "shell_select_change", "droid_death"}) {
        names.insert(core_name);
    }

    // Faction VO & soldier chatter.
    for (const char* side : {"all", "imp", "rep", "cis"}) {
        for (const char* suffix : {"_off_com_report_captured_commandpost", "_off_com_report_lost_commandpost",
                                   "_off_com_report_us_overwhelmed", "_off_com_report_enemy_losing",
                                   "_off_victory_im", "_off_defeat_im", "_inf_com_chatter_wound",
                                   "_inf_com_chatter_death"}) {
            names.insert(std::string(side) + suffix);
        }
    }

    // Per-planet/faction music cues.
    for (const char* side : {"all", "imp", "rep", "cis"}) {
        for (const char* planet : {"cor", "dag", "dea", "end", "fel", "geo", "hot", "kam", "kas", "mus", "myg",
                                   "nab", "pol", "tan", "tat", "uta", "yav"}) {
            for (const char* cue : {"start", "middle", "end", "victory", "defeat"}) {
                names.insert(std::string(side) + "_" + planet + "_amb_" + cue);
            }
        }
    }

    struct ExportedClip {
        std::string rel_path;
        std::string category;
        float weight = 1.0f;
    };
    std::map<std::tuple<std::string, uint32_t, uint32_t>, std::string> written_clips;
    size_t sfx_count = 0, vo_count = 0, music_count = 0;
    uint64_t total_bytes = 0;

    auto ensure_clip = [&](const SoundClipRef& clip) -> std::string {
        const std::string cat = clip.stream_hash == 0 ? "sfx" : (clip.channels > 1 ? "music" : "vo");
        const auto key = std::make_tuple(cat, clip.stream_hash, clip.sample_hash);
        if (const auto it = written_clips.find(key); it != written_clips.end()) {
            return it->second;
        }
        const std::string file_name = std::format("{:08x}.wav", clip.sample_hash);
        const std::string rel = cat + "/" + file_name;
        const std::filesystem::path dst = out_dir / cat / file_name;
        const std::vector<int16_t> pcm = decode_clip(clip);
        write_wav(dst, pcm, clip.sample_rate, clip.channels);
        total_bytes += std::filesystem::file_size(dst);
        if (cat == "sfx") ++sfx_count;
        else if (cat == "vo") ++vo_count;
        else ++music_count;
        written_clips.emplace(key, rel);
        return rel;
    };

    auto collect_clips = [&](const SoundProp& prop) -> std::pair<std::string, std::vector<ExportedClip>> {
        std::vector<ExportedClip> out;
        std::string cat = prop.kind == SoundProp::Kind::sample ? "sfx" : "vo";
        for (const auto& [hash, weight] : prop.entries) {
            const SoundClipRef* clip = prop.kind == SoundProp::Kind::sample
                                           ? library.find_sample(hash)
                                           : library.find_segment(prop.stream_hash, hash);
            if (!clip) {
                continue;
            }
            const std::string rel = ensure_clip(*clip);
            cat = clip->stream_hash == 0 ? "sfx" : (clip->channels > 1 ? "music" : "vo");
            out.push_back({rel, cat, weight});
        }
        return {cat, out};
    };

    std::ofstream json(out_dir / "sounds.json", std::ios::binary);
    json << "{\n  \"sounds\": {\n";
    size_t resolved_count = 0;
    for (const std::string& name : names) {
        const auto prop_opt = library.resolve(sound_name_hash(name));
        if (!prop_opt) {
            continue;
        }
        const SoundProp& prop = *prop_opt;
        if (prop.kind == SoundProp::Kind::layered) {
            struct ResolvedLayer {
                float delay = 0.0f;
                SoundProp child;
                std::vector<ExportedClip> clips;
            };
            std::vector<ResolvedLayer> layers;
            for (const auto& layer : prop.layers) {
                const auto child_opt = library.resolve(layer.prop_hash);
                if (!child_opt) {
                    continue;
                }
                auto [_, clips] = collect_clips(*child_opt);
                if (!clips.empty()) {
                    layers.push_back({layer.delay, *child_opt, std::move(clips)});
                }
            }
            if (layers.empty()) {
                continue;
            }
            json << (resolved_count++ ? ",\n" : "") << "    " << json_string(name) << ": {\"kind\": \"layered\", \"layers\": [";
            for (size_t i = 0; i < layers.size(); ++i) {
                const auto& l = layers[i];
                json << (i ? ", " : "")
                     << std::format("{{\"delay\": {:g}, \"gain\": {:g}, \"pitch\": {:g}, \"min_distance\": {:g}, "
                                    "\"max_distance\": {:g}, \"mode_3d\": {}, \"clips\": [",
                                    l.delay, l.child.gain, l.child.pitch, l.child.min_distance, l.child.max_distance,
                                    l.child.mode_3d ? "true" : "false");
                for (size_t c = 0; c < l.clips.size(); ++c) {
                    json << (c ? ", " : "")
                         << std::format("{{\"file\": {}, \"weight\": {:g}}}", json_string(l.clips[c].rel_path),
                                        l.clips[c].weight);
                }
                json << "]}";
            }
            json << "]}";
            continue;
        }

        auto [cat, clips] = collect_clips(prop);
        if (clips.empty()) {
            continue;
        }
        json << (resolved_count++ ? ",\n" : "") << "    " << json_string(name)
             << std::format(": {{\"kind\": {}, \"gain\": {:g}, \"gain_dev\": {:g}, \"pitch\": {:g}, \"pitch_dev\": {:g}, "
                            "\"min_distance\": {:g}, \"max_distance\": {:g}, \"rolloff\": {:g}, "
                            "\"play_probability\": {:g}, \"looping\": {}, \"mode_3d\": {}, \"clips\": [",
                            json_string(cat), prop.gain, prop.gain_dev, prop.pitch, prop.pitch_dev, prop.min_distance,
                            prop.max_distance, prop.rolloff, prop.play_probability, prop.looping ? "true" : "false",
                            prop.mode_3d ? "true" : "false");
        for (size_t c = 0; c < clips.size(); ++c) {
            json << (c ? ", " : "")
                 << std::format("{{\"file\": {}, \"weight\": {:g}}}", json_string(clips[c].rel_path), clips[c].weight);
        }
        json << "]}";
    }
    json << "\n  }\n}\n";

    std::cout << std::format("{} sounds ({} sfx, {} vo, {} music, {:.1f} MB)\n", resolved_count, sfx_count, vo_count,
                             music_count, double(total_bytes) / (1024.0 * 1024.0));
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
        if (command == "export-hud") {
            return cmd_export_hud(args);
        }
        if (command == "export-effects") {
            return cmd_export_effects(args);
        }
        if (command == "export-audio") {
            return cmd_export_audio(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
