#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bf2/ucfb.h"

namespace swm::bf2 {

// A munged ODF: an entity, ordnance, weapon or explosion class (chunks
// entc/ordc/wpnc/expc). TYPE is the class name, BASE its parent class or
// built-in class label, and each PROP a property: a name hash and a string
// value, in file order. Properties may repeat (lists like several sounds).
struct OdfClass {
    std::string kind;  // chunk tag
    std::string type;
    std::string base;
    std::vector<std::pair<uint32_t, std::string>> properties;

    // Last value of a property, by name.
    std::optional<std::string> get(std::string_view name) const;
};

// Every class in a munged file.
std::vector<OdfClass> read_odf_classes(const UcfbNode& root);

// Classes by name, for resolving inheritance across several files.
class OdfLibrary {
public:
    // Adds a file's classes. The first definition of a name wins.
    void add(const UcfbNode& root);
    const OdfClass* find(std::string_view type) const;
    // A class with its ancestors' properties first, so OdfClass::get()
    // returns the most derived value. `base` becomes the built-in class label
    // at the root of the chain (e.g. "soldier", "cannon", "bolt").
    OdfClass resolve(std::string_view type) const;

private:
    std::vector<OdfClass> classes_;
};

// Property name hash: 32-bit FNV-1a over the lowercase name.
uint32_t odf_hash(std::string_view name);

// The name of a known property, or empty. Known names come from a built-in
// list of properties documented for Battlefront II's mod tools.
std::string_view odf_property_name(uint32_t hash);

} // namespace swm::bf2
