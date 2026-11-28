#include "extract/cabs.h"

#include <memory>
#include <stdexcept>

extern "C" {
#include <libunshield.h>
}

#include "extract/process.h"

namespace fs = std::filesystem;

namespace swm::extract {
namespace {

struct UnshieldCloser {
    void operator()(Unshield* u) const { unshield_close(u); }
};
using UnshieldPtr = std::unique_ptr<Unshield, UnshieldCloser>;

// unshield takes narrow (ANSI code page) paths.
UnshieldPtr open_set(const fs::path& dir) {
    const fs::path hdr = dir / "data1.hdr";
    UnshieldPtr u(unshield_open(hdr.string().c_str()));
    if (!u) {
        throw std::runtime_error("cannot read the InstallShield set at " + path_utf8(hdr));
    }
    return u;
}

std::vector<CabFile> group_files(Unshield* u, const std::string& group, const fs::path& dir) {
    UnshieldFileGroup* g = unshield_file_group_find(u, group.c_str());
    if (!g) {
        throw std::runtime_error("no \"" + group + "\" file group in " + path_utf8(dir) +
                                 " (not the Galaxies discs?)");
    }
    std::vector<CabFile> files;
    for (unsigned i = g->first_file; i <= g->last_file; ++i) {
        if (!unshield_file_is_valid(u, int(i))) {
            continue;
        }
        const char* name = unshield_file_name(u, int(i));
        files.push_back({int(i), name ? name : "", unshield_file_size(u, int(i))});
    }
    return files;
}

} // namespace

std::vector<CabFile> list_cab_group(const fs::path& dir, const std::string& group) {
    UnshieldPtr u = open_set(dir);
    return group_files(u.get(), group, dir);
}

std::string extract_cab_group(const fs::path& dir, const fs::path& out_dir,
                              const std::function<void(size_t, size_t, const std::string&)>& on_file,
                              const std::atomic<bool>* cancel, const std::string& group) {
    try {
        UnshieldPtr u = open_set(dir);
        const std::vector<CabFile> files = group_files(u.get(), group, dir);
        fs::create_directories(out_dir);
        for (size_t n = 0; n < files.size(); ++n) {
            if (cancel && cancel->load()) {
                return "cancelled";
            }
            const CabFile& f = files[n];
            if (on_file) {
                on_file(n, files.size(), f.name);
            }
            const fs::path target = out_dir / f.name;
            std::error_code ec;
            if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == f.size) {
                continue;
            }
            const fs::path partial = target.string() + ".part";
            if (!unshield_file_save(u.get(), f.index, partial.string().c_str())) {
                fs::remove(partial, ec);
                return "could not extract " + f.name + " (a disc volume missing or unreadable? all of data1.cab "
                       "through the last data*.cab must be in " + path_utf8(dir) + ")";
            }
            fs::rename(partial, target, ec);
            if (ec) {
                return "could not write " + path_utf8(target) + ": " + ec.message();
            }
        }
        if (on_file) {
            on_file(files.size(), files.size(), "");
        }
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

} // namespace swm::extract
