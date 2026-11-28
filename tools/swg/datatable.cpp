#include "swg/datatable.h"

#include <format>
#include <stdexcept>

#include "swg/iff.h"

namespace swbfo::swg {

int DataTable::column(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == name) {
            return int(i);
        }
    }
    return -1;
}

std::string DataTable::text(size_t row, int column) const {
    if (column < 0 || row >= rows.size() || size_t(column) >= rows[row].size()) {
        return {};
    }
    const Value& v = rows[row][size_t(column)];
    if (const auto* s = std::get_if<std::string>(&v)) {
        return *s;
    }
    if (const auto* i = std::get_if<int32_t>(&v)) {
        return std::to_string(*i);
    }
    return std::format("{}", std::get<float>(v));
}

float DataTable::number(size_t row, int column) const {
    if (column < 0 || row >= rows.size() || size_t(column) >= rows[row].size()) {
        return 0;
    }
    const Value& v = rows[row][size_t(column)];
    if (const auto* f = std::get_if<float>(&v)) {
        return *f;
    }
    if (const auto* i = std::get_if<int32_t>(&v)) {
        return float(*i);
    }
    return 0;
}

DataTable parse_datatable(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "DTII") {
        throw std::runtime_error("not a datatable (" + root.tag + ")");
    }
    const IffNode& version = root.expect("0001");
    DataTable table;
    {
        IffReader r(version.expect("COLS").data);
        const int32_t count = r.i32();
        for (int32_t i = 0; i < count; ++i) {
            table.columns.push_back(r.cstring());
        }
    }
    {
        IffReader r(version.expect("TYPE").data);
        while (r.remaining() > 0 && table.types.size() < table.columns.size()) {
            table.types.push_back(r.cstring());
        }
        if (table.types.size() != table.columns.size()) {
            throw std::runtime_error("datatable: a type per column expected");
        }
    }
    IffReader r(version.expect("ROWS").data);
    const int32_t count = r.i32();
    for (int32_t i = 0; i < count; ++i) {
        std::vector<DataTable::Value> row;
        for (const std::string& type : table.types) {
            const char kind = type.empty() ? 's' : char(std::tolower(uint8_t(type[0])));
            if (kind == 's' || kind == 'p') {
                row.emplace_back(r.cstring());
            } else if (kind == 'f') {
                row.emplace_back(r.f32());
            } else {
                row.emplace_back(r.i32());
            }
        }
        table.rows.push_back(std::move(row));
    }
    return table;
}

} // namespace swbfo::swg
