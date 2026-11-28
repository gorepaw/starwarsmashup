#include "app.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include "extract/cabs.h"
#include "extract/process.h"
#include "extract/recipe.h"
#include "release_key.h"

namespace fs = std::filesystem;
using namespace swbfo::extract;

namespace swbfo::launcher {

namespace {

constexpr const char* manifest_name = "swg-total-experience-2005.tsv";

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string megabytes(uint64_t bytes) {
    return std::format("{:.0f} MB", double(bytes) / (1024.0 * 1024.0));
}

std::optional<fs::path> pick_folder() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD opts = 0;
    dialog->GetOptions(&opts);
    dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose a Battlefront II or Galaxies folder");
    std::optional<fs::path> out;
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return out;
}

void wrapped(const std::string& text) {
    ImGui::TextWrapped("%s", text.c_str());
}

bool big_button(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 14, ImGui::GetFontSize() * 2.2f));
    ImGui::EndDisabled();
    return pressed;
}

} // namespace

Config load_config(const fs::path& file) {
    Config c;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line.find('=') == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, line.find('=')));
        const std::string value = trim(line.substr(line.find('=') + 1));
        if (key == "channel") {
            c.channel = value;
        } else if (key == "url") {
            c.url = value;
            while (!c.url.empty() && c.url.back() == '/') {
                c.url.pop_back();
            }
        }
    }
    return c;
}

std::optional<Setup> load_setup(const fs::path& install_dir) {
    const fs::path file = install_dir / "state" / "setup.json";
    if (!fs::exists(file)) {
        return std::nullopt;
    }
    try {
        const auto j = nlohmann::json::parse(read_text(file));
        Setup s;
        s.bf2 = utf8_path(j.at("bf2").get<std::string>());
        for (const auto& p : j.at("swg")) {
            s.swg.push_back(utf8_path(p.get<std::string>()));
        }
        return s;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void save_setup(const fs::path& install_dir, const Setup& setup) {
    nlohmann::json j;
    j["bf2"] = path_utf8(setup.bf2);
    j["swg"] = nlohmann::json::array();
    for (const auto& p : setup.swg) {
        j["swg"].push_back(path_utf8(p));
    }
    fs::create_directories(install_dir / "state");
    std::ofstream(install_dir / "state" / "setup.json", std::ios::binary) << j.dump(1) << "\n";
}

App::App(fs::path launcher_exe) : exe_(std::move(launcher_exe)), dir_(exe_.parent_path()) {}

App::~App() {
    cancel_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void App::run_async(std::function<void()> work) {
    if (busy_) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    cancel_ = false;
    busy_ = true;
    worker_ = std::thread([this, work = std::move(work)] {
        try {
            work();
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            error_ = e.what();
            screen_ = Screen::failed;
        }
        busy_ = false;
    });
}

void App::start() {
    std::error_code ec;
    fs::remove(dir_ / "launcher.old.exe", ec);
    config_ = load_config(dir_ / "launcher.cfg");
    portable_ = fs::exists(dir_ / "portable.flag", ec);
    token_ = trim(read_text(dir_ / "portable.token"));
    run_async([this] {
        if (!config_.url.empty()) {
            do_update();
        } else {
            route();
        }
    });
}

bool App::game_installed() const {
    std::error_code ec;
    return fs::exists(dir_ / "game" / "SWBFO.exe", ec) && fs::exists(dir_ / "game" / "project.godot", ec);
}

void App::do_update() {
    screen_ = Screen::updating;
    {
        std::lock_guard lock(mutex_);
        status_ = "Checking for updates...";
    }
    const Remote remote{config_.url, config_.channel, token_};
    FetchedManifest fetched;
    try {
        fetched = fetch_manifest(remote, release_public_key);
    } catch (const std::exception& e) {
        if (game_installed()) {
            std::lock_guard lock(mutex_);
            notice_ = std::string("Couldn't check for updates: ") + e.what();
        } else {
            throw std::runtime_error(std::string("Couldn't download the game: ") + e.what());
        }
        route();
        return;
    }
    {
        std::lock_guard lock(mutex_);
        version_ = fetched.manifest.version;
    }
    const auto installed = installed_manifest(dir_);
    const UpdatePlan plan = plan_update(fetched.manifest, installed, dir_, "launcher.exe");
    if (fetched.manifest.min_launcher > launcher_build && !plan.replaces_launcher) {
        throw std::runtime_error("This update needs a newer launcher. Download it again from the project's page.");
    }
    if (!plan.empty()) {
        update_bytes_ = plan.bytes;
        {
            std::lock_guard lock(mutex_);
            status_ = std::format("Downloading version {}: {} files, {}", fetched.manifest.version,
                                  plan.download.size(), megabytes(plan.bytes));
        }
        const std::string err = apply_update(remote, plan, fetched.json, dir_, exe_, update_progress_, &cancel_);
        if (!err.empty()) {
            throw std::runtime_error("The update didn't finish: " + err + ". Start the launcher again to resume.");
        }
        if (plan.replaces_launcher) {
            // The new launcher carries on.
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + exe_.wstring() + L"\" --updated";
            if (CreateProcessW(exe_.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                               dir_.wstring().c_str(), &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                quit_ = true;
                return;
            }
        }
    } else if (!installed || installed->version != fetched.manifest.version) {
        std::ofstream(dir_ / "state" / "installed.json", std::ios::binary) << fetched.json;
    }
    route();
}

bool App::extraction_pending() const {
    const fs::path recipe_file = dir_ / "tools" / "recipe.json";
    if (!fs::exists(recipe_file)) {
        return false;
    }
    const Recipe recipe = load_recipe(recipe_file);
    const std::set<std::string> done = done_steps(dir_ / "state");
    return std::any_of(recipe.steps.begin(), recipe.steps.end(),
                       [&](const RecipeStep& s) { return !done.contains(s.key); });
}

void App::route() {
    if (!game_installed()) {
        throw std::runtime_error("The game's files are missing from " + path_utf8(dir_ / "game") +
                                 ". Start the launcher again to download them.");
    }
    if (portable_) {
        screen_ = Screen::ready;
        return;
    }
    const auto setup = load_setup(dir_);
    if (!setup) {
        screen_ = Screen::welcome;
        return;
    }
    if (extraction_pending()) {
        do_extract(*setup);
        return;
    }
    screen_ = Screen::ready;
}

void App::do_search(bool full) {
    screen_ = Screen::searching;
    done_ = 0;
    SearchOptions opt;
    opt.full = full;
    opt.cancel = &cancel_;
    opt.on_progress = [this](const fs::path& dir, size_t n) {
        done_ = n;
        std::lock_guard lock(mutex_);
        status_ = path_utf8(dir);
    };
    const auto found = find_sources(opt);
    std::lock_guard lock(mutex_);
    for (const auto& s : found) {
        const bool known = std::any_of(candidates_.begin(), candidates_.end(), [&](const Candidate& c) {
            return c.source.kind == s.kind && c.source.path == s.path;
        });
        if (!known) {
            candidates_.push_back({s, false});
        }
    }
    // Choose sensibly: the first Battlefront II, every usable Galaxies source.
    bool have_bf2 = std::any_of(candidates_.begin(), candidates_.end(),
                                [](const Candidate& c) { return c.chosen && c.source.kind == SourceKind::bf2; });
    for (auto& c : candidates_) {
        if (c.source.kind == SourceKind::bf2 && !have_bf2) {
            c.chosen = have_bf2 = true;
        } else if (c.source.kind == SourceKind::swg_archives || c.source.kind == SourceKind::swg_cabs) {
            c.chosen = true;
        }
    }
    searched_full_ = searched_full_ || full;
    screen_ = Screen::sources;
}

void App::add_folder() {
    const auto folder = pick_folder();
    if (!folder) {
        return;
    }
    const auto found = classify_folder(*folder);
    std::lock_guard lock(mutex_);
    if (found.empty()) {
        error_ = "Nothing usable in " + path_utf8(*folder) + ": choose Battlefront II's game folder, a folder of "
                 "Galaxies .tre archives, or a Total Experience disc.";
        return;
    }
    error_.clear();
    for (const auto& s : found) {
        candidates_.push_back({s, s.kind == SourceKind::swg_archives || s.kind == SourceKind::swg_cabs});
        if (s.kind == SourceKind::bf2) {
            for (auto& c : candidates_) {
                c.chosen = c.source.kind == SourceKind::bf2 ? c.source.path == s.path : c.chosen;
            }
        }
    }
}

void App::do_verify() {
    screen_ = Screen::verifying;
    std::vector<Candidate> chosen;
    {
        std::lock_guard lock(mutex_);
        for (const auto& c : candidates_) {
            if (c.chosen) {
                chosen.push_back(c);
            }
        }
        coverage_.clear();
        base_complete_ = false;
        log_.clear();
    }
    Setup setup;
    int disc = 0;
    for (const auto& c : chosen) {
        if (c.source.kind == SourceKind::bf2) {
            setup.bf2 = c.source.path;
        } else if (c.source.kind == SourceKind::swg_archives) {
            setup.swg.push_back(c.source.path);
        } else if (c.source.kind == SourceKind::swg_cabs) {
            // The discs' archives are copied out once and kept: later updates
            // read them again for new assets.
            const fs::path out = dir_ / "state" / "swg_discs" / std::to_string(disc++);
            const std::string err = extract_cab_group(
                c.source.path, out,
                [this](size_t done, size_t total, const std::string& name) {
                    done_ = done;
                    total_ = total;
                    std::lock_guard lock(mutex_);
                    status_ = "Reading the discs: " + name;
                },
                &cancel_);
            if (!err.empty()) {
                throw std::runtime_error("Couldn't read the Galaxies discs in " + path_utf8(c.source.path) + ": " +
                                         err);
            }
            setup.swg.push_back(out);
        }
    }
    {
        std::lock_guard lock(mutex_);
        status_ = "Checking every Galaxies file against SOE's originals...";
    }
    total_ = 0;
    const fs::path sources = dir_ / "state" / "verify.sources";
    write_sources_file(sources, dir_ / "tools" / manifest_name, setup.swg);
    const fs::path log = dir_ / "state" / "logs" / "verify.log";
    fs::create_directories(log.parent_path());
    std::error_code ec;
    fs::remove(log, ec);
    const int code = run_process({path_utf8(dir_ / "tools" / "swgtool.exe"), "verify", path_utf8(sources), "--missing",
                                  path_utf8(dir_ / "state" / "logs" / "missing_swg_files.tsv")},
                                 dir_, log, &cancel_);
    if (code == process_cancelled) {
        screen_ = Screen::sources;
        return;
    }
    std::istringstream lines(read_text(log));
    std::string line;
    std::lock_guard lock(mutex_);
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string tag, group;
        uint64_t found = 0, total = 0;
        if (words >> tag >> group >> found >> total && tag == "group") {
            coverage_.push_back({group, {found, total}});
        }
    }
    if (coverage_.empty()) {
        error_ = "Checking the Galaxies files failed: see " + path_utf8(log);
        screen_ = Screen::failed;
        return;
    }
    base_complete_ = code == 0;
    pending_ = setup;
}

void App::do_extract(const Setup& setup) {
    screen_ = Screen::extracting;
    save_setup(dir_, setup);
    const Recipe recipe = load_recipe(dir_ / "tools" / "recipe.json");
    write_sources_file(dir_ / "state" / "swg.sources", dir_ / "tools" / manifest_name, setup.swg);
    RunOptions opt;
    opt.install_dir = dir_;
    opt.state_dir = dir_ / "state";
    opt.placeholders.tools = dir_ / "tools";
    opt.placeholders.bf2 = setup.bf2;
    opt.placeholders.swg = dir_ / "state" / "swg.sources";
    opt.cancel = &cancel_;
    total_ = recipe.steps.size();
    done_ = 0;
    {
        std::lock_guard lock(mutex_);
        log_.clear();
    }
    opt.on_event = [this](const StepEvent& ev) {
        std::lock_guard lock(mutex_);
        if (ev.kind == StepEvent::started) {
            status_ = ev.step->label;
            return;
        }
        done_ = ev.index + 1;
        if (ev.kind == StepEvent::finished) {
            log_.push_back(std::format("ok    {}  ({:.1f}s)", ev.step->label, ev.seconds));
        } else if (ev.kind == StepEvent::failed) {
            log_.push_back(std::format("FAILED {}  (log: {})", ev.step->label, path_utf8(ev.log)));
        }
    };
    const RunResult r = run_recipe(recipe, opt);
    std::lock_guard lock(mutex_);
    if (r.cancelled) {
        error_ = "Extraction paused. Start the launcher again (or press Continue) to pick up where it stopped.";
        screen_ = Screen::failed;
    } else if (!r.ok()) {
        std::string why;
        for (size_t i = 0; i < r.failed.size() && i < 5; ++i) {
            why += "\n  " + r.failed[i];
        }
        if (!r.missing.empty()) {
            why += std::format("\n  {} expected files weren't made (first: {})", r.missing.size(), r.missing[0]);
        }
        error_ = std::format("{} of {} extraction steps failed:{}", r.failed.size(), recipe.steps.size(), why);
        screen_ = Screen::failed;
    } else {
        screen_ = Screen::ready;
    }
}

void App::play() {
    const fs::path game = dir_ / "game";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    const std::wstring exe = (game / "SWBFO.exe").wstring();
    std::wstring cmd = L"\"" + exe + L"\"";
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, game.wstring().c_str(), &si,
                       &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        quit_ = true;
    } else {
        std::lock_guard lock(mutex_);
        error_ = "Couldn't start the game (error " + std::to_string(GetLastError()) + ")";
    }
}

bool App::frame() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("SWBFO");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Battlefront II battles on the planets of Galaxies. An unofficial, non-commercial fan project.");
    ImGui::Separator();
    ImGui::Spacing();
    switch (screen_.load()) {
    case Screen::updating: draw_updating(); break;
    case Screen::welcome: draw_welcome(); break;
    case Screen::searching: draw_searching(); break;
    case Screen::sources: draw_sources(); break;
    case Screen::verifying: draw_verifying(); break;
    case Screen::extracting: draw_extracting(); break;
    case Screen::ready: draw_ready(); break;
    case Screen::failed: draw_failed(); break;
    }
    ImGui::End();
    return !quit_;
}

void App::draw_updating() {
    std::lock_guard lock(mutex_);
    wrapped(status_);
    if (update_bytes_ > 0) {
        const float f = float(double(update_progress_.bytes.load()) / double(update_bytes_));
        ImGui::ProgressBar(std::min(f, 1.0f), ImVec2(-1, 0),
                           std::format("{} of {}", megabytes(update_progress_.bytes), megabytes(update_bytes_)).c_str());
        if (ImGui::Button("Cancel")) {
            cancel_ = true;
        }
    }
}

void App::draw_welcome() {
    wrapped("This game is built from your own copies of Star Wars Battlefront II (2005) and Star Wars Galaxies. "
            "Nothing from either game comes with it: the launcher reads the files it needs from your installs and "
            "converts them, once (about 10 minutes and 5 GB).");
    ImGui::Spacing();
    wrapped("May the launcher look for them? It reads folder names, the Windows registry's list of installed "
            "programs, Steam's and GOG's records, and the headers of game archives. Nothing leaves this computer.");
    ImGui::Spacing();
    wrapped("Galaxies files can come from the Total Experience discs, an old SOE install, or a SWGEmu or SWG "
            "Legends client folder: only files identical to SOE's originals are used. SWG Restoration's archives "
            "are encrypted and can't be used.");
    ImGui::Spacing();
    if (big_button("Look in the usual places")) {
        run_async([this] { do_search(false); });
    }
    ImGui::SameLine();
    if (big_button("Search every drive")) {
        run_async([this] { do_search(true); });
    }
    ImGui::SameLine();
    if (big_button("I'll choose the folders")) {
        screen_ = Screen::sources;
    }
}

void App::draw_searching() {
    ImGui::TextUnformatted("Looking for Battlefront II and Galaxies...");
    if (done_ > 0) {
        std::lock_guard lock(mutex_);
        ImGui::Text("%llu folders read", static_cast<unsigned long long>(done_.load()));
        ImGui::TextDisabled("%s", status_.c_str());
    }
    if (ImGui::Button("Stop searching")) {
        cancel_ = true;
    }
}

void App::draw_sources() {
    std::lock_guard lock(mutex_);
    const auto has = [&](SourceKind k) {
        return std::any_of(candidates_.begin(), candidates_.end(),
                           [&](const Candidate& c) { return c.chosen && c.source.kind == k; });
    };
    const bool bf2 = has(SourceKind::bf2);
    const bool swg = has(SourceKind::swg_archives) || has(SourceKind::swg_cabs);
    wrapped(candidates_.empty() ? "Nothing found yet. Add the folders yourself, or search every drive."
                                : "Found these. Tick the ones to use: one Battlefront II, and any Galaxies sources "
                                  "(they fill each other's gaps).");
    ImGui::Spacing();
    if (ImGui::BeginChild("##found", ImVec2(0, -ImGui::GetFontSize() * 6), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < candidates_.size(); ++i) {
            Candidate& c = candidates_[i];
            ImGui::PushID(int(i));
            const bool usable = c.source.kind == SourceKind::bf2 || c.source.kind == SourceKind::swg_archives ||
                                c.source.kind == SourceKind::swg_cabs;
            const char* what = c.source.kind == SourceKind::bf2 || c.source.kind == SourceKind::bf2_unsupported
                                   ? "Battlefront II"
                                   : "Galaxies";
            ImGui::BeginDisabled(!usable);
            if (ImGui::Checkbox("##use", &c.chosen) && c.chosen && c.source.kind == SourceKind::bf2) {
                for (auto& other : candidates_) {
                    if (&other != &c && other.source.kind == SourceKind::bf2) {
                        other.chosen = false;
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("%s: %s", what, path_utf8(c.source.path).c_str());
            ImGui::Indent(ImGui::GetFontSize() * 2);
            ImGui::TextDisabled("%s (found by: %s)", c.source.note.c_str(), c.source.found_by.c_str());
            ImGui::Unindent(ImGui::GetFontSize() * 2);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
    }
    if (!bf2) {
        ImGui::TextDisabled("Choose a Battlefront II install (its game folder).");
    } else if (!swg) {
        ImGui::TextDisabled("Choose at least one Galaxies source.");
    }
    if (big_button("Add a folder...")) {
        mutex_.unlock();
        add_folder();
        mutex_.lock();
    }
    ImGui::SameLine();
    if (!searched_full_ && big_button("Search every drive")) {
        run_async([this] { do_search(true); });
    }
    ImGui::SameLine();
    if (big_button("Check the files", bf2 && swg)) {
        error_.clear();
        run_async([this] { do_verify(); });
    }
}

void App::draw_verifying() {
    std::lock_guard lock(mutex_);
    if (busy_) {
        wrapped(status_);
        if (total_ > 0) {
            ImGui::ProgressBar(float(double(done_) / double(total_)), ImVec2(-1, 0));
        }
        if (ImGui::Button("Cancel")) {
            cancel_ = true;
        }
        return;
    }
    ImGui::TextUnformatted("Galaxies files identical to SOE's originals:");
    const char* names[][2] = {{"base", "Galaxies (needed)"}, {"jtl", "Jump to Lightspeed (optional)"},
                              {"rotw", "Rage of the Wookiees (optional)"}, {"tow", "Trials of Obi-Wan (optional)"}};
    for (const auto& [group, counts] : coverage_) {
        const char* label = group.c_str();
        for (const auto& n : names) {
            label = group == n[0] ? n[1] : label;
        }
        const bool all = counts.first == counts.second;
        ImGui::TextColored(all ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(1, 0.7f, 0.4f, 1), "  %-34s %llu of %llu",
                           label, static_cast<unsigned long long>(counts.first),
                           static_cast<unsigned long long>(counts.second));
    }
    ImGui::Spacing();
    if (base_complete_) {
        wrapped("Everything the game needs is there. Extracting takes about 10 minutes and 5 GB.");
        if (big_button("Extract")) {
            const Setup setup = pending_;
            run_async([this, setup] { do_extract(setup); });
        }
    } else {
        wrapped("Some Galaxies files the game needs aren't among these folders as SOE shipped them (the list is in "
                "state\\logs\\missing_swg_files.tsv). Add another source, such as the Total Experience discs.");
    }
    ImGui::SameLine();
    if (big_button("Back")) {
        screen_ = Screen::sources;
    }
}

void App::draw_extracting() {
    std::lock_guard lock(mutex_);
    ImGui::TextUnformatted("Extracting assets from your games...");
    const uint64_t total = std::max<uint64_t>(total_, 1);
    ImGui::ProgressBar(float(double(done_) / double(total)), ImVec2(-1, 0),
                       std::format("{} of {} steps", done_.load(), total_.load()).c_str());
    ImGui::TextDisabled("Now: %s", status_.c_str());
    if (ImGui::BeginChild("##log", ImVec2(0, -ImGui::GetFontSize() * 3), ImGuiChildFlags_Borders)) {
        for (const auto& l : log_) {
            ImGui::TextUnformatted(l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Pause")) {
        cancel_ = true;
    }
}

void App::draw_ready() {
    std::lock_guard lock(mutex_);
    ImGui::TextUnformatted("Ready.");
    if (!version_.empty()) {
        ImGui::TextDisabled("Version %s (%s)%s", version_.c_str(), config_.channel.c_str(),
                            portable_ ? ", portable build" : "");
    }
    if (!notice_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "%s", notice_.c_str());
    }
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
    }
    ImGui::Spacing();
    if (big_button("Play")) {
        mutex_.unlock();
        play();
        mutex_.lock();
    }
    if (!portable_) {
        ImGui::SameLine();
        if (big_button("Change game folders")) {
            screen_ = Screen::welcome;
        }
    }
    ImGui::SameLine();
    if (big_button("Open the logs")) {
        const fs::path logs = dir_ / "state" / "logs";
        fs::create_directories(logs);
        ShellExecuteW(nullptr, L"open", logs.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void App::draw_failed() {
    std::lock_guard lock(mutex_);
    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Something went wrong.");
    wrapped(error_);
    ImGui::Spacing();
    const auto setup = portable_ ? std::nullopt : load_setup(dir_);
    if (setup && game_installed()) {
        if (big_button("Continue")) {
            error_.clear();
            const Setup s = *setup;
            run_async([this, s] { do_extract(s); });
        }
        ImGui::SameLine();
    }
    if (!portable_ && game_installed()) {
        if (big_button("Choose game folders")) {
            error_.clear();
            screen_ = Screen::welcome;
        }
        ImGui::SameLine();
    }
    if (big_button("Open the logs")) {
        const fs::path logs = dir_ / "state" / "logs";
        fs::create_directories(logs);
        ShellExecuteW(nullptr, L"open", logs.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (big_button("Quit")) {
        quit_ = true;
    }
}

} // namespace swbfo::launcher
