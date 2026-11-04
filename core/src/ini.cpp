#include "swm/core/ini.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace swm::core {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::optional<std::string> Ini::Section::get(std::string_view key) const {
    std::optional<std::string> found;
    for (const auto& [entry, value] : values) {
        if (same(entry, key)) {
            found = value;
        }
    }
    return found;
}

double Ini::Section::number(std::string_view key, double fallback) const {
    const auto text = get(key);
    if (!text) {
        return fallback;
    }
    double value = fallback;
    const std::string_view s = trim(*text);
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    return error == std::errc() ? value : fallback;
}

Ini Ini::parse(std::string_view text) {
    Ini ini;
    ini.sections_.push_back({});  // values before any [section]
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            ini.sections_.push_back({std::string(trim(line.substr(1, line.size() - 2))), {}});
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        std::string_view value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        ini.sections_.back().values.emplace_back(std::string(trim(line.substr(0, equals))), std::string(value));
    }
    return ini;
}

Ini Ini::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return parse(text);
}

const Ini::Section* Ini::section(std::string_view name) const {
    for (const Section& section : sections_) {
        if (same(section.name, name)) {
            return &section;
        }
    }
    return nullptr;
}

} // namespace swm::core
