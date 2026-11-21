#pragma once

#include <map>
#include <string>

#include "swg/tre_library.h"

namespace swm::swg {

// What an object template (object/.../shared_*.iff) says about how an
// object looks: its appearance (.apt, .msh, .lod, .cmp, .sat, ...) and,
// for a building, its portal layout (.pob). Empty where it names none.
struct SharedObjectTemplate {
    std::string appearance;
    std::string portal_layout;
};

// Reads the shared template's parameters. A template sets some parameters
// and leaves the rest to the template it derives from (DERV), which is read
// in turn. Throws std::runtime_error if a template can't be read.
//
// Parameter chunks (XXXX) are the parameter's name, a byte saying whether
// this template sets it (0: no, the base's applies), then the value: for a
// file name, a null-terminated string.
class ObjectTemplates {
public:
    explicit ObjectTemplates(TreLibrary& library) : library_(library) {}
    const SharedObjectTemplate& shared(const std::string& path);

private:
    struct Read {
        SharedObjectTemplate values;
        bool has_appearance = false, has_portal_layout = false;
        std::string base;
    };
    Read read(const std::string& path);

    TreLibrary& library_;
    std::map<std::string, SharedObjectTemplate> cache_;
};

} // namespace swm::swg
