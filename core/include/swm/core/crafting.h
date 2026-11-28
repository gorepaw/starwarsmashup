#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "swm/core/ini.h"
#include "swm/core/items.h"

namespace swm::core {

// Wave 7A gathering & crafting (docs/PROFESSIONS.md):
// Plain-English resource categories & stats, 1-step schematics with optional
// enhancement slots, visible experimentation odds, Recipe Presets, Material
// Vault stacks, Hub Work Orders, and Salvaging.

enum class ResourceCategory : uint8_t { mineral, chemical, gas, flora, water, organic, salvage };
inline constexpr size_t resource_category_count = 7;

enum class ResourceStat : uint8_t { quality, conductivity, toughness, malleability, purity };
inline constexpr size_t resource_stat_count = 5;

std::optional<ResourceCategory> resource_category_from_name(std::string_view name);
std::string_view resource_category_name(ResourceCategory category);
std::string_view resource_category_label(ResourceCategory category);

std::optional<ResourceStat> resource_stat_from_name(std::string_view name);
std::string_view resource_stat_name(ResourceStat stat);
std::string_view resource_stat_label(ResourceStat stat);

struct ResourceDef {
    std::string id;
    std::string name;
    ResourceCategory category = ResourceCategory::mineral;
    std::string profession = "surveying";  // surveying | foraging | salvaging
    std::array<int, resource_stat_count> stats{};  // 0..1000
    uint32_t seed = 1;
    double peak_x = 0.0;  // game space
    double peak_z = 0.0;  // game space
    double radius = 350.0;
};

struct MaterialStack {
    std::string resource_id;
    int64_t amount = 0;

    friend bool operator==(const MaterialStack&, const MaterialStack&) = default;
};

struct SchematicSlot {
    ResourceCategory category = ResourceCategory::mineral;
    int amount = 0;
};

struct StatWeight {
    ResourceStat stat = ResourceStat::quality;
    double weight = 0.0;
};

struct SchematicDef {
    std::string id;
    std::string name;
    std::string group;  // what the workbench files it under ("Pistols", "Light Armor")
    std::string profession = "weaponsmith";
    int required_level = 1;
    std::string output_item;
    int output_count = 1;
    int64_t base_xp = 60;
    int enhancement_slots = 1;  // 0..2 optional enhancement modules
    std::vector<SchematicSlot> slots;
    std::vector<StatWeight> weights;
    bool marks = false;  // its output takes Mk tiers (item_takes_marks)
    // `Blueprint = yes`: hidden and unmakeable until its blueprint is learned
    // (an item with `Teaches = <this id>`, used); otherwise everyone knows it.
    bool blueprint = false;
};

// Mk tiers from crafting (docs/CRAFTING.md): a weapon, armor or a Deflector
// Shield is made at any mark up to its crafter's level's (Mk I at 10 ... Mk X
// at 100), costing more materials and giving more XP the higher it is.
bool item_takes_marks(const ItemDef& def);
int craft_max_mark(int crafter_level);
int mark_material_amount(int amount, int mark);    // amount x (1 + 0.25 a mark), rounded up
// Experimentation Focus (docs/CRAFTING.md, Grades): 0..100%, better odds of a
// better grade for more materials, up to 10x at 100%.
double focus_material_mult(int focus_pct);  // 1 + 9 x focus
// A slot's materials for a craft: its amount, for its mark and focus, rounded up.
int craft_material_amount(int amount, int mark, int focus_pct);
int64_t mark_craft_xp(int64_t base_xp, int mark);  // x (1 + 0.15 a mark)

// Dyes (docs/CRAFTING.md): an owned piece's colors, its model's customization
// values as a query string, "index_color_1=5&index_color_2=12": at most
// max_dye_values, names of [a-z0-9_/], indices 0..255 ("" is no dye). A dye
// costs dye_cost units of one chemical.
inline constexpr size_t max_dye_values = 6;
inline constexpr int64_t dye_cost = 2;
bool dye_values_ok(std::string_view dye);

struct RecipePreset {
    std::string name;
    std::string schematic_id;
    std::vector<std::string> resource_ids;
    std::vector<std::string> enhancement_defs;
    uint8_t exp_quality_pct = 0;  // its Experimentation Focus

    friend bool operator==(const RecipePreset&, const RecipePreset&) = default;
};

// A craft's odds of each grade (docs/CRAFTING.md, Grades), summing to 1.
struct ExperimentOdds {
    double flawed = 0.25;
    double standard = 0.50;
    double fine = 0.20;
    double exceptional = 0.05;
};

struct CraftPreview {
    double material_score = 0.5;  // 0..1: its materials' stats, as its schematic weighs them
    double material_mult = 1.0;   // its materials x this, for its focus (focus_material_mult)
    ExperimentOdds odds;
};

struct ScanResult {
    std::string best_resource_id;
    std::string best_resource_name;
    double concentration = 0.0;
    double peak_x = 0.0;
    double peak_z = 0.0;
    int grid_n = 0;
    double step = 0.0;
    std::vector<float> grid;  // row-major grid_n x grid_n concentration values in 0..1
};

enum class AutoFillMode : uint8_t { best_quality, best_stat, cheapest };

class CraftingBook {
public:
    void load_resources(const std::filesystem::path& path);
    // Its [schematic]s, and its [family]s' (docs/CRAFTING.md,
    // Schematics): one schematic for every catalog item a family matches (by
    // kind, archetype, armor class, slot, id), unless a [schematic] makes it.
    // The catalog is its [catalog] Path, relative to the file.
    void load_schematics(const std::filesystem::path& path);
    void parse_resources(const Ini& ini, const std::string& where);
    void parse_schematics(const Ini& ini, const std::string& where, const ItemCatalog* catalog = nullptr);

    const std::vector<ResourceDef>& resources() const { return resources_; }
    const ResourceDef* find_resource(std::string_view id) const;
    const std::vector<SchematicDef>& schematics() const { return schematics_; }
    const SchematicDef* find_schematic(std::string_view id) const;

    // Deterministic 0..1 topographic concentration field for a resource at (x, z) in game space.
    double concentration_at(const ResourceDef& def, double x, double z) const;
    // Highest-concentration resource of `category` at (x, z).
    std::pair<const ResourceDef*, double> best_resource_at(ResourceCategory category, double x, double z) const;
    // Samples a centered grid_n x grid_n patch of radius `radius` around (x, z).
    ScanResult scan_terrain(ResourceCategory category, double x, double z, double radius = 60.0, int grid_n = 17) const;

    // Picks resource_ids from `vault` for each slot of `schematic` (empty
    // string if missing), enough for its mark and focus (craft_material_amount).
    std::vector<std::string> auto_fill(const SchematicDef& schematic, std::span<const MaterialStack> vault,
                                       AutoFillMode mode, int mark = 0, int focus_pct = 0) const;

    // The grade odds: its materials' score (0..1), its crafter's level (and
    // Lv 75 Experimentation milestone), its modules (0..2) and its focus all
    // move them toward the better grades.
    // grade_bonus: where the roll centers moves up (a crafting perk's
    // Experimentation: 0.15 each); exceptional_mult: the Exceptional odds
    // times that, the rest scaled to fit (Masterwork: 2).
    static ExperimentOdds grade_odds(double material_score, int crafter_level, int enhancement_count, int focus_pct,
                                     double grade_bonus = 0.0, double exceptional_mult = 1.0);
    CraftPreview preview_craft(const SchematicDef& schematic, std::span<const std::string> resource_ids,
                               int enhancement_count, int crafter_level, uint8_t focus_pct,
                               double grade_bonus = 0.0, double exceptional_mult = 1.0) const;
    // A craft's grade for a 0..1 roll (Flawed to Exceptional).
    static ItemGrade roll_craft_grade(const CraftPreview& preview, double unit_roll);

    struct SalvageResult {
        std::vector<MaterialStack> materials;
        int64_t xp = 0;
    };
    // yield_bonus: a Salvaging perk's more from each salvage (0.2: 20%).
    SalvageResult salvage_yield(const ItemDef& def, int salvaging_level, double yield_bonus = 0.0) const;

private:
    std::vector<ResourceDef> resources_;
    std::vector<SchematicDef> schematics_;
};

int64_t material_amount(std::span<const MaterialStack> vault, std::string_view resource_id);
void add_material(std::vector<MaterialStack>& vault, std::string_view resource_id, int64_t delta);

// Crafting stations (a hub's [workbench] Type, net::Workbench::kind) make
// their professions' schematics only, as Galaxies' did: "weapon" (Weapon &
// Droid) a weaponsmith's and droid engineer's, "clothing" (Armor & Clothing)
// an armorsmith's and tailor's, "food" (Food & Chemical) a chef's, doctor's
// and bio-engineer's, "structure" (Structure & Machinery) an architect's;
// "general" everything.
std::string_view station_for(std::string_view profession);  // its kind ("general" for one none names)
bool station_makes(std::string_view station_kind, std::string_view profession);
std::string_view station_name(std::string_view station_kind);  // "Weapon & Droid Crafting Station"

} // namespace swm::core
