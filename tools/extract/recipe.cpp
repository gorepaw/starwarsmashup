#include "extract/recipe.h"

#include <chrono>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "extract/process.h"

namespace swm::extract {

Recipe parse_recipe(const std::string& text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("recipe: ") + e.what());
    }
    Recipe recipe;
    recipe.format = j.value("format", 0);
    if (recipe.format != 1) {
        throw std::runtime_error(std::format("recipe: format {} not supported (this extractor reads 1)", recipe.format));
    }
    recipe.commit = j.value("commit", "");
    for (const auto& s : j.at("steps")) {
        RecipeStep step;
        step.phase = s.at("phase").get<std::string>();
        step.label = s.at("label").get<std::string>();
        step.key = s.at("key").get<std::string>();
        step.argv = s.at("argv").get<std::vector<std::string>>();
        if (step.argv.empty()) {
            throw std::runtime_error("recipe: step " + step.label + " has no command");
        }
        if (s.contains("copy")) {
            step.copy_from = s["copy"].at("from").get<std::string>();
            step.copy_to_dir = s["copy"].at("to_dir").get<std::string>();
        }
        recipe.steps.push_back(std::move(step));
    }
    recipe.required = j.value("required", std::vector<std::string>{});
    return recipe;
}

Recipe load_recipe(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path_utf8(file));
    }
    std::stringstream text;
    text << in.rdbuf();
    return parse_recipe(text.str());
}

std::string expand(const std::string& text, const Placeholders& p) {
    const std::pair<const char*, const std::filesystem::path*> names[] = {
        {"{tools}", &p.tools}, {"{bf2}", &p.bf2}, {"{swg}", &p.swg}, {"{scratch}", &p.scratch}};
    std::string out = text;
    for (const auto& [name, value] : names) {
        const std::string v = path_utf8(*value);
        for (size_t pos = out.find(name); pos != std::string::npos; pos = out.find(name, pos + v.size())) {
            out.replace(pos, std::char_traits<char>::length(name), v);
        }
    }
    return out;
}

std::set<std::string> done_steps(const std::filesystem::path& state_dir) {
    std::set<std::string> done;
    std::ifstream in(state_dir / "extracted.txt", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            done.insert(line);
        }
    }
    return done;
}

void mark_done(const std::filesystem::path& state_dir, const std::string& key) {
    std::filesystem::create_directories(state_dir);
    std::ofstream out(state_dir / "extracted.txt", std::ios::binary | std::ios::app);
    out << key << '\n';
}

std::vector<std::string> missing_required(const Recipe& recipe, const std::filesystem::path& install_dir) {
    std::vector<std::string> missing;
    for (const std::string& rel : recipe.required) {
        const std::filesystem::path p = install_dir / utf8_path(rel);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) && !std::filesystem::exists(p.string() + ".r32", ec)) {
            missing.push_back(rel);
        }
    }
    return missing;
}

namespace {

std::string safe_name(const std::string& label) {
    std::string out;
    for (const char c : label) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '-' || c == '_';
        out += ok ? c : '_';
    }
    return out;
}

} // namespace

RunResult run_recipe(const Recipe& recipe, const RunOptions& options) {
    RunResult result;
    Placeholders placeholders = options.placeholders;
    if (placeholders.scratch.empty()) {
        placeholders.scratch = options.state_dir / "scratch";
    }
    const std::filesystem::path log_dir = options.state_dir / "logs";
    std::filesystem::create_directories(log_dir);
    std::filesystem::create_directories(placeholders.scratch);
    const std::set<std::string> done = options.force ? std::set<std::string>{} : done_steps(options.state_dir);

    const size_t total = recipe.steps.size();
    for (size_t i = 0; i < total; ++i) {
        const RecipeStep& step = recipe.steps[i];
        if (!options.phases.empty() && !options.phases.contains(step.phase)) {
            continue;
        }
        StepEvent ev;
        ev.index = i;
        ev.total = total;
        ev.step = &step;
        if (done.contains(step.key)) {
            ++result.skipped;
            ev.kind = StepEvent::skipped;
            if (options.on_event) {
                options.on_event(ev);
            }
            continue;
        }
        if (options.cancel && options.cancel->load()) {
            result.cancelled = true;
            break;
        }
        std::vector<std::string> argv;
        for (const std::string& a : step.argv) {
            argv.push_back(expand(a, placeholders));
        }
        // argv[0] relative to the install dir (the recipe's {tools} is absolute).
        if (!utf8_path(argv[0]).is_absolute()) {
            argv[0] = path_utf8(options.install_dir / utf8_path(argv[0]));
        }
        ev.log = log_dir / std::format("{:03}_{}.log", i + 1, safe_name(step.label));
        {
            std::ofstream log(ev.log, std::ios::binary | std::ios::trunc);
            log << narrow(command_line(argv)) << "\n\n";
        }
        ev.kind = StepEvent::started;
        if (options.on_event) {
            options.on_event(ev);
        }
        const auto t0 = std::chrono::steady_clock::now();
        int code = run_process(argv, options.install_dir, ev.log, options.cancel);
        if (code == 0 && !step.copy_from.empty()) {
            const std::filesystem::path from = utf8_path(expand(step.copy_from, placeholders));
            const std::filesystem::path to = options.install_dir / utf8_path(expand(step.copy_to_dir, placeholders));
            std::error_code ec;
            std::filesystem::create_directories(to, ec);
            if (!std::filesystem::copy_file(from, to / from.filename(),
                                            std::filesystem::copy_options::overwrite_existing, ec)) {
                std::ofstream(ev.log, std::ios::binary | std::ios::app)
                    << "\ncould not copy " << path_utf8(from) << ": " << ec.message() << "\n";
                code = 1;
            }
        }
        ev.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        ev.exit_code = code;
        if (code == process_cancelled) {
            result.cancelled = true;
            ev.kind = StepEvent::failed;
            if (options.on_event) {
                options.on_event(ev);
            }
            break;
        }
        if (code == 0) {
            mark_done(options.state_dir, step.key);
            ++result.ran;
            ev.kind = StepEvent::finished;
        } else {
            result.failed.push_back(std::format("{} (log: {})", step.label, path_utf8(ev.log)));
            ev.kind = StepEvent::failed;
        }
        if (options.on_event) {
            options.on_event(ev);
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(placeholders.scratch, ec);
    if (!result.cancelled && options.phases.empty()) {
        result.missing = missing_required(recipe, options.install_dir);
    }
    return result;
}

} // namespace swm::extract
