#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace swm::core {

// A small INI document: [sections] of `key = value` lines, in file order.
// Keys may repeat. Lines starting with ';' or '#' are comments. This is the
// format of our data files (and close to Battlefront's ODFs).
class Ini {
public:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> values;

        // Last value of a key (case-insensitive), if any.
        std::optional<std::string> get(std::string_view key) const;
        double number(std::string_view key, double fallback) const;
    };

    static Ini parse(std::string_view text);
    // Throws std::runtime_error if the file can't be read.
    static Ini load(const std::filesystem::path& path);

    const std::vector<Section>& sections() const { return sections_; }
    // First section with this name (case-insensitive), or nullptr.
    const Section* section(std::string_view name) const;

private:
    std::vector<Section> sections_;
};

} // namespace swm::core
