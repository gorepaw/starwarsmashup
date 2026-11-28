#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace swbfo::extract {

// The InstallShield file group holding Galaxies' archives on the Total
// Experience discs (docs/ARCHITECTURE.md, Supported game versions).
inline constexpr const char* swg_cab_group = "Star Wars files";

struct CabFile {
    int index = 0;
    std::string name;  // file name inside the group
    size_t size = 0;
};

// The group's files in an InstallShield set (data1.hdr and its data*.cab
// volumes, all in `dir`). Throws std::runtime_error if it can't be opened or
// has no such group.
std::vector<CabFile> list_cab_group(const std::filesystem::path& dir, const std::string& group = swg_cab_group);

// Extracts the group's files into out_dir, as unshield does (the discs'
// reference was made with unshield 1.6.2, so the archives come out byte-exact).
// Calls on_file before each one (done so far, total, name). Returns an error
// message, or empty on success. Files already there with the right size are
// kept, so a cancelled run resumes.
std::string extract_cab_group(const std::filesystem::path& dir, const std::filesystem::path& out_dir,
                              const std::function<void(size_t, size_t, const std::string&)>& on_file = {},
                              const std::atomic<bool>* cancel = nullptr,
                              const std::string& group = swg_cab_group);

} // namespace swbfo::extract
