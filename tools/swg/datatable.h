#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace swm::swg {

// A datatable (.iff, DTII 0001): named, typed columns and rows of values.
// Column types are letters with optional defaults: s (string), i (integer),
// f (float), b (boolean, stored as an integer), h (a hash, an integer), e
// (an enumeration, an integer). Every cell is a string, an integer or a
// float, by its column's type.
struct DataTable {
    using Value = std::variant<std::string, int32_t, float>;
    std::vector<std::string> columns;
    std::vector<std::string> types;
    std::vector<std::vector<Value>> rows;

    // The column's index, or -1.
    int column(const std::string& name) const;
    // A cell as text (numbers formatted).
    std::string text(size_t row, int column) const;
    float number(size_t row, int column) const;
};

// Throws std::runtime_error on unsupported or malformed data.
DataTable parse_datatable(std::span<const uint8_t> bytes);

} // namespace swm::swg
