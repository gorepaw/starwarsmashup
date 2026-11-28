// swbfo_extract: the launcher's extraction steps from the command line
// (docs/DISTRIBUTION.md). Finds Battlefront II and Galaxies installs, reads the
// Total Experience discs' cabinets, checks Galaxies files against the reference
// manifest, and runs the extraction recipe.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "extract/cabs.h"
#include "extract/discovery.h"
#include "extract/process.h"
#include "extract/recipe.h"

namespace fs = std::filesystem;
using namespace swbfo::extract;

namespace {

constexpr const char* usage = R"(usage:
  swbfo_extract find [--full]                       look for installs (registry, known folders, discs; --full: every drive)
  swbfo_extract classify <dir>                      what a folder is
  swbfo_extract cabs <cab_dir> <out_dir>            Galaxies' archives from the Total Experience discs' cabinets
  swbfo_extract verify --tools <dir> --swg <dir>... [--manifest <tsv>]
                                                  how much of the reference the Galaxies folders supply
  swbfo_extract run --install <dir> --tools <dir> --bf2 <_lvl_pc> --swg <dir>... [--recipe <json>]
                  [--manifest <tsv>] [--state <dir>] [--phase <p>]... [--force]
                                                  run the recipe (defaults: <tools>\recipe.json, <tools>\
                                                  swg-total-experience-2005.tsv, <install>\state)
)";

std::atomic<bool> g_cancel{false};

BOOL WINAPI on_ctrl(DWORD) {
    g_cancel = true;
    return TRUE;
}

std::vector<std::string> utf8_args() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        out.push_back(narrow(argv[i]));
    }
    LocalFree(argv);
    return out;
}

std::string option(const std::vector<std::string>& args, const std::string& flag) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) {
            return args[i + 1];
        }
    }
    return {};
}

std::vector<std::string> options(const std::vector<std::string>& args, const std::string& flag) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) {
            out.push_back(args[i + 1]);
        }
    }
    return out;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    return std::find(args.begin(), args.end(), flag) != args.end();
}

void print_sources(const std::vector<FoundSource>& sources) {
    for (const FoundSource& s : sources) {
        std::cout << std::format("{:<16} {:<18} {}\n{:<36} {}\n", kind_name(s.kind), s.found_by, path_utf8(s.path), "",
                                 s.note);
    }
    if (sources.empty()) {
        std::cout << "nothing found\n";
    }
}

int cmd_find(const std::vector<std::string>& args) {
    SearchOptions opt;
    opt.full = has_flag(args, "--full");
    opt.cancel = &g_cancel;
    opt.on_progress = [](const fs::path& dir, size_t n) {
        std::cerr << std::format("\r{} folders read: {:.70}", n, path_utf8(dir)) << std::string(10, ' ');
    };
    const auto sources = find_sources(opt);
    if (opt.full) {
        std::cerr << "\n";
    }
    print_sources(sources);
    return 0;
}

int cmd_classify(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << usage;
        return 2;
    }
    print_sources(classify_folder(utf8_path(args[0])));
    return 0;
}

int cmd_cabs(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::string err = extract_cab_group(
        utf8_path(args[0]), utf8_path(args[1]),
        [](size_t done, size_t total, const std::string& name) {
            std::cerr << std::format("\r{}/{} {:<40}", done, total, name);
        },
        &g_cancel);
    std::cerr << "\n";
    if (!err.empty()) {
        std::cerr << err << "\n";
        return 1;
    }
    return 0;
}

fs::path default_manifest(const fs::path& tools) {
    return tools / "swg-total-experience-2005.tsv";
}

int cmd_verify(const std::vector<std::string>& args) {
    const std::string tools = option(args, "--tools");
    const std::vector<std::string> swg = options(args, "--swg");
    if (tools.empty() || swg.empty()) {
        std::cerr << usage;
        return 2;
    }
    const fs::path manifest = option(args, "--manifest").empty() ? default_manifest(utf8_path(tools))
                                                                 : utf8_path(option(args, "--manifest"));
    std::vector<fs::path> folders;
    for (const auto& s : swg) {
        folders.push_back(utf8_path(s));
    }
    const fs::path tmp = fs::temp_directory_path() / "swbfo_extract_verify";
    fs::create_directories(tmp);
    write_sources_file(tmp / "swg.sources", manifest, folders);
    const fs::path log = tmp / "verify.log";
    fs::remove(log);
    const int code = run_process({path_utf8(utf8_path(tools) / "swgtool.exe"), "verify", path_utf8(tmp / "swg.sources")},
                                 fs::current_path(), log, &g_cancel);
    std::ifstream in(log, std::ios::binary);
    std::cout << in.rdbuf();
    return code;
}

int cmd_run(const std::vector<std::string>& args) {
    const std::string install = option(args, "--install");
    const std::string tools = option(args, "--tools");
    const std::string bf2 = option(args, "--bf2");
    const std::vector<std::string> swg = options(args, "--swg");
    if (install.empty() || tools.empty() || bf2.empty() || swg.empty()) {
        std::cerr << usage;
        return 2;
    }
    const fs::path tools_dir = utf8_path(tools);
    const fs::path recipe_file = option(args, "--recipe").empty() ? tools_dir / "recipe.json"
                                                                  : utf8_path(option(args, "--recipe"));
    const fs::path manifest = option(args, "--manifest").empty() ? default_manifest(tools_dir)
                                                                 : utf8_path(option(args, "--manifest"));
    RunOptions opt;
    opt.install_dir = fs::absolute(utf8_path(install));
    opt.state_dir = option(args, "--state").empty() ? opt.install_dir / "state" : utf8_path(option(args, "--state"));
    opt.force = has_flag(args, "--force");
    opt.cancel = &g_cancel;
    for (const auto& p : options(args, "--phase")) {
        opt.phases.insert(p);
    }
    std::vector<fs::path> folders;
    for (const auto& s : swg) {
        folders.push_back(fs::absolute(utf8_path(s)));
    }
    write_sources_file(opt.state_dir / "swg.sources", fs::absolute(manifest), folders);
    opt.placeholders.tools = fs::absolute(tools_dir);
    opt.placeholders.bf2 = fs::absolute(utf8_path(bf2));
    opt.placeholders.swg = opt.state_dir / "swg.sources";
    opt.on_event = [](const StepEvent& ev) {
        const char* what = ev.kind == StepEvent::started    ? "..  "
                           : ev.kind == StepEvent::finished ? "ok  "
                           : ev.kind == StepEvent::failed   ? "FAIL"
                                                            : "done";
        if (ev.kind == StepEvent::started) {
            return;
        }
        std::cout << std::format("[{:>3}/{}] {} {:<8} {:<40} {:6.1f}s\n", ev.index + 1, ev.total, what,
                                 ev.step->phase, ev.step->label, ev.seconds)
                  << std::flush;
    };
    const Recipe recipe = load_recipe(recipe_file);
    std::cout << std::format("recipe {} ({} steps), state {}\n", recipe.commit.substr(0, 10), recipe.steps.size(),
                             path_utf8(opt.state_dir));
    const RunResult r = run_recipe(recipe, opt);
    std::cout << std::format("\nran {}, already done {}, failed {}\n", r.ran, r.skipped, r.failed.size());
    for (const auto& f : r.failed) {
        std::cout << "  failed " << f << "\n";
    }
    for (size_t i = 0; i < r.missing.size() && i < 30; ++i) {
        std::cout << "  missing " << r.missing[i] << "\n";
    }
    if (r.missing.size() > 30) {
        std::cout << std::format("  ... {} missing in all\n", r.missing.size());
    }
    if (r.cancelled) {
        std::cout << "cancelled: run again to resume\n";
    }
    return r.ok() ? 0 : 1;
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    const std::vector<std::string> all = utf8_args();
    if (all.size() < 2) {
        std::cerr << usage;
        return 2;
    }
    const std::string command = all[1];
    const std::vector<std::string> args(all.begin() + 2, all.end());
    try {
        if (command == "find") {
            return cmd_find(args);
        }
        if (command == "classify") {
            return cmd_classify(args);
        }
        if (command == "cabs") {
            return cmd_cabs(args);
        }
        if (command == "verify") {
            return cmd_verify(args);
        }
        if (command == "run") {
            return cmd_run(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << usage;
    return 2;
}
