#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace swm::extract {

// The launcher's extraction recipe, written by
// tools/assets/rebuild_imported_assets.py --emit-recipe (docs/DISTRIBUTION.md).
struct RecipeStep {
    std::string phase;
    std::string label;
    std::string key;                // changes when the step's command or inputs do
    std::vector<std::string> argv;  // with {tools} {bf2} {swg} {scratch} placeholders
    // Copy `copy_from` into the folder `copy_to_dir` after the command succeeds.
    std::string copy_from;
    std::string copy_to_dir;
};

struct Recipe {
    int format = 0;
    std::string commit;
    std::vector<RecipeStep> steps;
    // Paths (relative to the install dir) that must exist afterwards; a
    // terrain bake's prefix counts if <prefix>.r32 does.
    std::vector<std::string> required;
};

// Throws std::runtime_error on a malformed or unsupported recipe.
Recipe parse_recipe(const std::string& json);
Recipe load_recipe(const std::filesystem::path& file);

struct Placeholders {
    std::filesystem::path tools;    // bf2tool.exe, swgtool.exe
    std::filesystem::path bf2;      // Battlefront II's GameData\Data\_lvl_pc
    std::filesystem::path swg;      // a folder of .tre archives or a .sources file
    std::filesystem::path scratch;  // a temp folder
};

// `text` with each {name} replaced (UTF-8).
std::string expand(const std::string& text, const Placeholders& placeholders);

struct StepEvent {
    enum Kind { started, finished, failed, skipped };
    Kind kind = started;
    size_t index = 0;  // in the recipe
    size_t total = 0;
    const RecipeStep* step = nullptr;
    double seconds = 0;
    std::filesystem::path log;
    int exit_code = 0;
};

struct RunOptions {
    std::filesystem::path install_dir;  // laid out like the repo root; steps run here
    std::filesystem::path state_dir;    // done-steps file, logs, scratch
    Placeholders placeholders;          // scratch is filled in if empty
    std::set<std::string> phases;       // empty: all
    bool force = false;                 // run steps already done
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const StepEvent&)> on_event;
};

struct RunResult {
    size_t ran = 0;
    size_t skipped = 0;
    std::vector<std::string> failed;   // "label (log: ...)"
    std::vector<std::string> missing;  // required paths that don't exist
    bool cancelled = false;
    bool ok() const { return failed.empty() && missing.empty() && !cancelled; }
};

// Runs every step not done yet, in order (later steps can read earlier ones'
// output: a planet's people stand on its terrain bake). A step's key is
// recorded in <state_dir>/extracted.txt when it succeeds, so an interrupted
// run resumes and an updated recipe runs only new or changed steps. Then
// checks the required paths.
RunResult run_recipe(const Recipe& recipe, const RunOptions& options);

// The keys recorded as done, and recording one.
std::set<std::string> done_steps(const std::filesystem::path& state_dir);
void mark_done(const std::filesystem::path& state_dir, const std::string& key);

// Required paths missing under install_dir.
std::vector<std::string> missing_required(const Recipe& recipe, const std::filesystem::path& install_dir);

} // namespace swm::extract
