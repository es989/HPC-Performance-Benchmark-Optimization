#ifndef SIZE_PARSE_HPP
#define SIZE_PARSE_HPP

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>

// Small helper to parse size strings like: 64MB, 512KiB, 1GB, 1048576
// Intended for benchmark inputs (not a general-purpose parser).
inline std::uint64_t parse_size_bytes(const std::string& text) {
    auto trim = [](const std::string& s) -> std::string {
        std::size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        std::size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    };

    const std::string s = trim(text);
    if (s.empty()) throw std::invalid_argument("empty size string");

    // Split numeric prefix and unit suffix.
    std::size_t i = 0;
    bool seen_dot = false;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '.' && !seen_dot) {
            seen_dot = true;
            ++i;
            continue;
        }
        break;
    }

    if (i == 0) throw std::invalid_argument("size has no numeric prefix");

    const double value = std::stod(s.substr(0, i));
    std::string unit = trim(s.substr(i));

    // Normalize unit to lower-case.
    for (char& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::uint64_t mult = 1;
    if (unit.empty() || unit == "b") {
        mult = 1;
    } else if (unit == "kb") {
        mult = 1000ull;
    } else if (unit == "mb") {
        mult = 1000ull * 1000ull;
    } else if (unit == "gb") {
        mult = 1000ull * 1000ull * 1000ull;
    } else if (unit == "kib" || unit == "ki") {
        mult = 1024ull;
    } else if (unit == "mib" || unit == "mi") {
        mult = 1024ull * 1024ull;
    } else if (unit == "gib" || unit == "gi") {
        mult = 1024ull * 1024ull * 1024ull;
    } else {
        throw std::invalid_argument("unsupported unit: " + unit);
    }

    if (value < 0.0) throw std::invalid_argument("size must be non-negative");

    const long double bytes = static_cast<long double>(value) * static_cast<long double>(mult);
    if (bytes > static_cast<long double>(UINT64_MAX)) throw std::overflow_error("size overflows uint64");

    return static_cast<std::uint64_t>(bytes + 0.5L); // round to nearest byte
}

#endif // SIZE_PARSE_HPP
