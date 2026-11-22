#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "swg/tre_library.h"

namespace swm::swg {

// What an object template (object/.../shared_*.iff) says about how an
// object looks: its appearance (.apt, .msh, .lod, .cmp, .sat, ...) and,
// for a building, its portal layout (.pob). Empty where it names none.
struct SharedObjectTemplate {
    std::string appearance;
    std::string portal_layout;
    std::string client_data;  // .cdf: a person's clothes and hair, sounds, effects
    // A creature's (SCOT) body: the radius and height it collides with,
    // meters; 0 if no template in its chain sets one.
    float collision_radius = 0, collision_height = 0;
};

// What a client data file (.cdf, CLDF) says a person wears: the meshes in
// its WEARs (.lmg or .mgn; a WEAR may hold several, a suit of armor's
// pieces), worn over its appearance. Throws on malformed data.
std::vector<std::string> client_data_wearables(std::span<const uint8_t> bytes);

// Reads the shared template's parameters. A template sets some parameters
// and leaves the rest to the template it derives from (DERV), which is read
// in turn. Throws std::runtime_error if a template can't be read.
//
// Parameter chunks (XXXX) are the parameter's name, a byte saying whether
// this template sets it (0: no, the base's applies), then the value: for a
// file name, a null-terminated string; for a number, a byte saying what
// kind of value follows and, for a single one (' ', the only kind read:
// ASSUMPTION, from the creature templates, where a set radius is a float
// after it), a 32-bit float.
class ObjectTemplates {
public:
    explicit ObjectTemplates(TreLibrary& library) : library_(library) {}
    const SharedObjectTemplate& shared(const std::string& path);

private:
    struct Read {
        SharedObjectTemplate values;
        bool has_appearance = false, has_portal_layout = false, has_client_data = false, has_radius = false,
             has_height = false;
        std::string base;
    };
    Read read(const std::string& path);

    TreLibrary& library_;
    std::map<std::string, SharedObjectTemplate> cache_;
};

} // namespace swm::swg
