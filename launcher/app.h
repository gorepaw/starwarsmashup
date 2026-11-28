#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "extract/discovery.h"
#include "update.h"

namespace swbfo::launcher {

// launcher.cfg next to launcher.exe: "key = value" lines.
struct Config {
    std::string channel = "stable";
    std::string url;  // update server base URL; empty: no updates
};
Config load_config(const std::filesystem::path& file);

// The game folders the player chose (state/setup.json), kept so updates can
// extract new assets without asking again.
struct Setup {
    std::filesystem::path bf2;
    std::vector<std::filesystem::path> swg;  // archive folders, including extracted discs
};
std::optional<Setup> load_setup(const std::filesystem::path& install_dir);
void save_setup(const std::filesystem::path& install_dir, const Setup& setup);

class App {
public:
    explicit App(std::filesystem::path launcher_exe);
    ~App();

    void start();
    // Draws the current screen (inside an ImGui frame). Returns false to quit.
    bool frame();

private:
    enum class Screen { updating, welcome, searching, sources, verifying, extracting, ready, failed };

    struct Candidate {
        extract::FoundSource source;
        bool chosen = false;
    };

    // Worker-thread steps (blocking).
    void do_update();
    void route();  // after updates: setup, extraction or ready
    void do_search(bool full);
    void do_verify();
    void do_extract(const Setup& setup);

    void run_async(std::function<void()> work);  // main thread, when idle
    void add_folder();
    bool extraction_pending() const;
    bool game_installed() const;
    void play();

    void draw_updating();
    void draw_welcome();
    void draw_searching();
    void draw_sources();
    void draw_verifying();
    void draw_extracting();
    void draw_ready();
    void draw_failed();

    std::filesystem::path exe_;
    std::filesystem::path dir_;  // install dir
    Config config_;
    bool portable_ = false;
    std::string token_;

    std::atomic<Screen> screen_{Screen::updating};
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> quit_{false};

    std::mutex mutex_;  // guards the strings and lists below
    std::string status_;
    std::string error_;
    std::vector<std::string> log_;
    std::vector<Candidate> candidates_;
    std::vector<std::pair<std::string, std::pair<uint64_t, uint64_t>>> coverage_;  // group, found/total
    bool base_complete_ = false;
    bool searched_full_ = false;
    std::string version_;
    std::string notice_;  // shown on the ready screen (e.g. offline)
    Setup pending_;       // chosen on the sources screen, saved once extraction starts
    std::atomic<bool> busy_{false};

    // Progress.
    std::atomic<uint64_t> done_{0};
    std::atomic<uint64_t> total_{0};
    UpdateProgress update_progress_;
    uint64_t update_bytes_ = 0;
};

} // namespace swbfo::launcher
