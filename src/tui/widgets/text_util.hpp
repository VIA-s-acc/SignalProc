#pragma once

#include <algorithm>
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

inline bool is_combining_mark(const std::string& cp) {
    if (cp.size() == 2) {
        const auto value = (static_cast<unsigned>(static_cast<unsigned char>(cp[0]) & 0x1F) << 6) | (static_cast<unsigned char>(cp[1]) & 0x3F);
        return value >= 0x300 && value <= 0x36F;
    }
    if (cp.size() == 3) {
        const auto value = (static_cast<unsigned>(static_cast<unsigned char>(cp[0]) & 0x0F) << 12) | ((static_cast<unsigned>(static_cast<unsigned char>(cp[1]) & 0x3F)) << 6) |
                           (static_cast<unsigned char>(cp[2]) & 0x3F);
        return (value >= 0x20D0 && value <= 0x20FF) || (value >= 0xFE20 && value <= 0xFE2F);
    }
    return false;
}

inline int display_width(const std::string& s) {
    int width = 0;
    for (const auto& cp : split_codepoints(s))
        if (!is_combining_mark(cp)) ++width;
    return width;
}

inline std::string ellipsize_start(const std::string& s, int width, bool ascii) {
    if (width <= 0) return {};
    if (display_width(s) <= width) return s;
    const auto cps = split_codepoints(s);
    std::size_t first = cps.size();
    int used = 0;
    while (first > 0) {
        const bool mark = is_combining_mark(cps[first - 1]);
        if (!mark && used + 1 >= width) break;
        --first;
        if (!mark) ++used;
    }
    while (first < cps.size() && is_combining_mark(cps[first])) ++first;
    std::string out = ascii ? "~" : "…";
    for (std::size_t i = first; i < cps.size(); ++i) out += cps[i];
    return out;
}

inline std::vector<std::string> wrap_codepoints(const std::string& s, int width, const std::string& indent) {
    std::vector<std::string> out;
    const auto cps = split_codepoints(s);
    std::string line;
    int used = 0, room = std::max(1, width);
    for (const auto& cp : cps) {
        if (!is_combining_mark(cp) && used >= room) {
            out.push_back(std::move(line));
            line = indent;
            used = display_width(indent);
            room = std::max(used + 1, width);
        }
        line += cp;
        if (!is_combining_mark(cp)) ++used;
    }
    out.push_back(std::move(line));
    return out;
}

inline std::vector<std::string> wrap_words(const std::string& s, int width) {
    const int room = std::max(1, width);
    std::vector<std::string> out;
    std::string line;
    int used = 0;
    std::size_t at = 0;
    while (at <= s.size()) {
        const auto space = s.find(' ', at);
        const auto word = s.substr(at, space == std::string::npos ? std::string::npos : space - at);
        const int w = display_width(word);
        if (used > 0 && used + 1 + w > room) {
            out.push_back(std::move(line));
            line.clear();
            used = 0;
        }
        if (w > room) {
            for (auto& part : wrap_codepoints(word, room, "")) {
                if (used > 0) {
                    out.push_back(std::move(line));
                    line.clear();
                    used = 0;
                }
                used = display_width(part);
                line = std::move(part);
            }
        } else {
            if (used > 0) line += ' ', ++used;
            line += word;
            used += w;
        }
        if (space == std::string::npos) break;
        at = space + 1;
    }
    out.push_back(std::move(line));
    return out;
}

inline std::string ellipsize(const std::string& s, int width, bool ascii) {
    if (width <= 0) return {};
    if (display_width(s) <= width) return s;
    std::string out;
    int used = 0;
    for (const auto& cp : split_codepoints(s)) {
        const bool mark = is_combining_mark(cp);
        if (!mark && used + 1 >= width) break;
        out += cp;
        if (!mark) ++used;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
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
