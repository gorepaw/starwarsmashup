#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace swm::extract {

// UTF-8 <-> UTF-16 for Windows APIs.
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& utf16);
// A path as UTF-8, and back.
std::string path_utf8(const std::filesystem::path& path);
std::filesystem::path utf8_path(const std::string& utf8);

// The command line Windows programs parse back into `argv` (the MSVC runtime's
// quoting rules).
std::wstring command_line(const std::vector<std::string>& argv);

constexpr int process_not_started = -1000;
constexpr int process_cancelled = -1001;

// Runs argv (argv[0] the program's path) in `cwd` with no window, its output
// and errors appended to `log`. Waits for it; `cancel` turning true kills it
// and everything it started. Returns its exit code, process_not_started, or
// process_cancelled.
int run_process(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                const std::filesystem::path& log, const std::atomic<bool>* cancel = nullptr);

} // namespace swm::extract
