// The extractor's pieces on synthetic folders (no game data): command-line
// quoting, recipes, resuming, folder classification.

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "extract/discovery.h"
#include "extract/process.h"
#include "extract/recipe.h"

namespace fs = std::filesystem;
using namespace swbfo::extract;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

void test_command_line_round_trips() {
    const std::vector<std::string> argv = {"C:\\a b\\tool.exe", "plain", "", "with space", "quote\"inside",
                                           "trail\\", "C:\\dir with space\\", "back\\\\\"slash", "ünïcode"};
    int n = 0;
    wchar_t** parsed = CommandLineToArgvW(command_line(argv).c_str(), &n);
    CHECK(n == int(argv.size()));
    for (int i = 0; i < n && i < int(argv.size()); ++i) {
        CHECK(narrow(parsed[i]) == argv[size_t(i)]);
    }
    LocalFree(parsed);
}

void test_recipe_runs_and_resumes() {
    const fs::path root = fs::temp_directory_path() / "swbfo_extract_test";
    fs::remove_all(root);
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::string cmd = path_utf8(fs::path(system) / "cmd.exe");
    // Step 1 writes a file under the install dir; step 2 writes into {scratch}
    // and the recipe copies it out; step 3 fails.
    std::string json = R"({"format": 1, "commit": "abc", "steps": [
      {"phase": "bf2", "label": "one", "key": "k1", "argv": [")" + cmd + R"(", "/c", "echo {bf2}> out1.txt"]},
      {"phase": "bf2", "label": "two", "key": "k2", "argv": [")" + cmd + R"(", "/c", "echo two> {scratch}\\two.ini"],
       "copy": {"from": "{scratch}\\two.ini", "to_dir": "maps\\x"}},
      {"phase": "swg", "label": "three", "key": "k3", "argv": [")" + cmd + R"(", "/c", "exit 3"]}
    ], "required": ["out1.txt", "maps\\x\\two.ini", "terrain\\bake", "nothing.txt"]})";
    // JSON wants the command path's backslashes escaped.
    std::string fixed;
    {
        std::string esc_cmd;
        for (char c : cmd) {
            esc_cmd += c == '\\' ? std::string("\\\\") : std::string(1, c);
        }
        size_t pos = 0;
        fixed = json;
        while ((pos = fixed.find(cmd, pos)) != std::string::npos) {
            fixed.replace(pos, cmd.size(), esc_cmd);
            pos += esc_cmd.size();
        }
    }
    const Recipe recipe = parse_recipe(fixed);
    CHECK(recipe.steps.size() == 3);
    CHECK(recipe.steps[1].copy_to_dir == "maps\\x");
    CHECK(recipe.required.size() == 4);

    write(root / "install" / "terrain" / "bake.r32", "x");  // a bake prefix counts by its .r32
    RunOptions opt;
    opt.install_dir = root / "install";
    opt.state_dir = root / "state";
    opt.placeholders.bf2 = "C:\\BF2 here";
    std::vector<std::string> seen;
    opt.on_event = [&](const StepEvent& ev) {
        if (ev.kind != StepEvent::started) {
            seen.push_back(ev.step->label + (ev.kind == StepEvent::finished ? "+" : ev.kind == StepEvent::skipped ? "=" : "!"));
        }
    };
    RunResult r = run_recipe(recipe, opt);
    CHECK(r.ran == 2);
    CHECK(r.failed.size() == 1);
    CHECK(r.missing == std::vector<std::string>{"nothing.txt"});
    CHECK((seen == std::vector<std::string>{"one+", "two+", "three!"}));
    std::ifstream out1(root / "install" / "out1.txt");
    std::string line;
    std::getline(out1, line);
    out1.close();
    CHECK(line == "C:\\BF2 here");
    CHECK(fs::exists(root / "install" / "maps" / "x" / "two.ini"));
    CHECK(!fs::exists(root / "state" / "scratch"));
    CHECK(done_steps(root / "state") == std::set<std::string>({"k1", "k2"}));

    // Again: the done steps are skipped, the failed one tried again.
    seen.clear();
    r = run_recipe(recipe, opt);
    CHECK(r.ran == 0 && r.skipped == 2 && r.failed.size() == 1);
    CHECK((seen == std::vector<std::string>{"one=", "two=", "three!"}));

    // One phase only; and --force.
    seen.clear();
    opt.phases = {"bf2"};
    opt.force = true;
    r = run_recipe(recipe, opt);
    CHECK(r.ran == 2 && r.failed.empty() && r.missing.empty());
    fs::remove_all(root);
}

void test_placeholders() {
    Placeholders p;
    p.tools = "C:\\t";
    p.swg = "D:\\s.sources";
    CHECK(expand("{tools}\\swgtool.exe", p) == "C:\\t\\swgtool.exe");
    CHECK(expand("{swg}|{swg}|{other}", p) == "D:\\s.sources|D:\\s.sources|{other}");
}

void test_classify_folders() {
    const fs::path root = fs::temp_directory_path() / "swbfo_classify_test";
    fs::remove_all(root);
    // Battlefront II's game folder (GameData\Data\_lvl_pc is found from it).
    write(root / "Star Wars Battlefront II Classic" / "GameData" / "Data" / "_lvl_pc" / "ingame.lvl", "x");
    write(root / "Star Wars Battlefront II Classic" / "GameData" / "Data" / "_lvl_pc" / "mission.lvl", "x");
    // The Classic Collection.
    write(root / "STAR WARS Battlefront Classic Collection" / "_lvl_pc" / "ingame.lvl", "x");
    write(root / "STAR WARS Battlefront Classic Collection" / "_lvl_pc" / "mission.lvl", "x");
    // An emulator's client: SOE archives, its own, and a non-archive .tre.
    write(root / "SWGEmu" / "bottom.tre", std::string("EERT5000") + std::string(40, '\0'));
    write(root / "SWGEmu" / "custom.tre", std::string("EERT5000") + std::string(40, '\0'));
    write(root / "SWGEmu" / "junk.tre", "hello");
    // Restoration's.
    write(root / "SWG Restoration" / "SwgRestoration_00.tre", std::string("EERT6000") + std::string(40, '\0'));
    // Disc cabinets.
    write(root / "disc" / "data1.hdr", "x");
    write(root / "disc" / "data1.cab", "x");

    auto one = [&](const char* dir) { return classify_folder(root / dir); };
    auto bf2 = one("Star Wars Battlefront II Classic");
    CHECK(bf2.size() == 1 && bf2[0].kind == SourceKind::bf2);
    CHECK(!bf2.empty() && bf2[0].path.filename() == "_lvl_pc");
    auto cc = one("STAR WARS Battlefront Classic Collection");
    CHECK(cc.size() == 1 && cc[0].kind == SourceKind::bf2_unsupported);
    auto emu = one("SWGEmu");
    CHECK(emu.size() == 1 && emu[0].kind == SourceKind::swg_archives && emu[0].archives == 2);
    auto res = one("SWG Restoration");
    CHECK(res.size() == 1 && res[0].kind == SourceKind::swg_encrypted);
    auto disc = one("disc");
    CHECK(disc.size() == 1 && disc[0].kind == SourceKind::swg_cabs);
    // The parent folder finds its children's (one level down).
    CHECK(classify_folder(root).size() == 5);
    CHECK(classify_folder(root / "nope").empty());

    write_sources_file(root / "x.sources", "C:\\m.tsv", {root / "SWGEmu", root / "disc"});
    std::ifstream in(root / "x.sources");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(text.find("manifest C:\\m.tsv\n") != std::string::npos);
    CHECK(text.find("source " + path_utf8(root / "SWGEmu") + "\n") != std::string::npos);
    fs::remove_all(root);
}

} // namespace

int main() {
    try {
        test_command_line_round_trips();
        test_placeholders();
        test_recipe_runs_and_resumes();
        test_classify_folders();
    } catch (const std::exception& e) {
        std::printf("FAIL: exception: %s\n", e.what());
        ++failures;
    }
    if (failures == 0) {
        std::printf("extract_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
