#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace signals::test {

inline std::string_view own_text(std::string_view body) {
    for (std::size_t at = 0; at < body.size();) {
        if (body[at] == '#' && (at == 0 || body[at - 1] == '\n')) return body.substr(0, at);
        const auto next = body.find('\n', at);
        at = next == std::string_view::npos ? body.size() : next + 1;
    }
    return body;
}

inline std::vector<std::string> math_spans(std::string_view body) {
    std::vector<std::string> out;
    bool fence = false;
    std::string text;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('\n', start);
        const auto line = body.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.starts_with("```")) fence = !fence;
        else if (!fence) {
            text += line;
            text += '\n';
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '`') {
            const auto close = text.find('`', i + 1);
            i = close == std::string::npos ? text.size() : close + 1;
            continue;
        }
        if (text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
        }
        if (text[i] != '$') {
            ++i;
            continue;
        }
        const bool display = i + 1 < text.size() && text[i + 1] == '$';
        const std::string_view delim = display ? "$$" : "$";
        std::size_t j = i + delim.size();
        while (j < text.size()) {
            if (text[j] == '\\') {
                j += 2;
                continue;
            }
            if (text.compare(j, delim.size(), delim) == 0) break;
            ++j;
        }
        if (j >= text.size()) break;
        out.push_back(text.substr(i + delim.size(), j - i - delim.size()));
        i = j + delim.size();
    }
    return out;
}


}  // namespace signals::test
