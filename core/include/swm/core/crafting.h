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
};

// Mk tiers from crafting (docs/CRAFTING.md): a weapon, armor or a Deflector
// Shield is made at any mark up to its crafter's level's (Mk I at 10 ... Mk X
// at 100), costing more materials and giving more XP the higher it is.
bool item_takes_marks(const ItemDef& def);
int craft_max_mark(int crafter_level);
int mark_material_amount(int amount, int mark);    // amount x (1 + 0.25 a mark), rounded up
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
    uint8_t exp_quality_pct = 50;

    friend bool operator==(const RecipePreset&, const RecipePreset&) = default;
};

struct WorkOrder {
    std::string id;
    std::string title;
    std::string profession = "weaponsmith";
    std::string output_item;
    int count = 1;
    int64_t reward_credits = 0;
    int64_t reward_fp = 0;
    int64_t reward_xp = 0;
};

struct ExperimentOdds {
    double great_success = 0.10;
    double success = 0.50;
    double neutral = 0.25;
    double minor_flaw = 0.15;
};

struct CraftPreview {
    double base_quality = 0.5;
    double min_quality = 0.4;
    double max_quality = 0.6;
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
    // Its [schematic]s and [work_order]s, and its [family]s' (docs/CRAFTING.md,
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
    const std::vector<WorkOrder>& work_orders() const { return work_orders_; }
    const WorkOrder* find_work_order(std::string_view id) const;

    // Deterministic 0..1 topographic concentration field for a resource at (x, z) in game space.
    double concentration_at(const ResourceDef& def, double x, double z) const;
    // Highest-concentration resource of `category` at (x, z).
    std::pair<const ResourceDef*, double> best_resource_at(ResourceCategory category, double x, double z) const;
    // Samples a centered grid_n x grid_n patch of radius `radius` around (x, z).
    ScanResult scan_terrain(ResourceCategory category, double x, double z, double radius = 60.0, int grid_n = 17) const;

    // Picks resource_ids from `vault` for each slot of `schematic` (empty
    // string if missing), enough for its mark (mark_material_amount).
    std::vector<std::string> auto_fill(const SchematicDef& schematic, std::span<const MaterialStack> vault,
                                       AutoFillMode mode, int mark = 0) const;

    static ExperimentOdds experiment_odds(int crafter_level);
    CraftPreview preview_craft(const SchematicDef& schematic, std::span<const std::string> resource_ids,
                               int enhancement_count, int crafter_level, uint8_t exp_quality_pct) const;
    static std::pair<double, std::string> roll_craft_quality(const CraftPreview& preview, double unit_roll);

    struct SalvageResult {
        std::vector<MaterialStack> materials;
        int64_t xp = 0;
    };
    SalvageResult salvage_yield(const ItemDef& def, int salvaging_level) const;

private:
    std::vector<ResourceDef> resources_;
    std::vector<SchematicDef> schematics_;
    std::vector<WorkOrder> work_orders_;
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
