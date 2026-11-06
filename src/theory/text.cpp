#include "theory/text.hpp"

namespace signals::theory {

std::u32string to_u32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + n > s.size()) n = 1;
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string to_utf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

bool is_combining(char32_t c) { return (c >= 0x300 && c <= 0x36F) || (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F); }

std::size_t display_width(std::string_view utf8) {
    std::size_t w = 0;
    for (char32_t c : to_u32(utf8))
        if (!is_combining(c)) ++w;
    return w;
}

std::string fold_case(std::string_view utf8) {
    auto text = to_u32(utf8);
    for (auto& c : text) {
        if (c >= U'A' && c <= U'Z') c += 32;
        else if (c >= 0x0410 && c <= 0x042F) c += 0x20;
        else if (c == 0x0401 || c == 0x0451) c = 0x0435;
    }
    return to_utf8(text);
}

}  // namespace signals::theory
