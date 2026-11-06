#pragma once

#include <string>
#include <vector>

namespace signals::tui {

inline std::string utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

inline std::vector<std::string> split_codepoints(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

inline int display_width(const std::string& s) { return static_cast<int>(split_codepoints(s).size()); }

inline std::string ellipsize(const std::string& s, int width, bool ascii) {
    if (width <= 0) return {};
    const auto cps = split_codepoints(s);
    if (static_cast<int>(cps.size()) <= width) return s;
    std::string out;
    for (int i = 0; i + 1 < width; ++i) out += cps[static_cast<std::size_t>(i)];
    return out + (ascii ? "~" : "…");
}

inline std::string pad_right(std::string s, int width) {
    const int n = display_width(s);
    if (n < width) s.append(static_cast<std::size_t>(width - n), ' ');
    return s;
}

inline std::string pad_left(std::string s, int width) {
    const int n = display_width(s);
    return n < width ? std::string(static_cast<std::size_t>(width - n), ' ') + s : s;
}

inline std::string superscript_count(int n) {
    static const char* digits[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string out;
    for (char c : std::to_string(n)) out += digits[c - '0'];
    return out;
}

inline std::string subscript_count(std::size_t n) {
    static const char* digits[] = {"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
    std::string out;
    for (char c : std::to_string(n)) out += digits[c - '0'];
    return out;
}

}  // namespace signals::tui
