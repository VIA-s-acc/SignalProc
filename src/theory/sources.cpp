#include "theory/sources.hpp"

#include <cctype>
#include <string>
#include <vector>

struct SignalsEmbeddedFile {
    const char* name;
    const char* text;
};
extern const SignalsEmbeddedFile kSignalsCardSources[];
extern const std::size_t kSignalsCardSourcesCount;

namespace signals::theory {

namespace {

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

bool names(std::string_view line, std::string_view name, bool call) {
    for (auto at = line.find(name); at != std::string_view::npos; at = line.find(name, at + 1)) {
        auto end = at + name.size();
        if ((at > 0 && word_char(line[at - 1])) || (end < line.size() && word_char(line[end]))) continue;
        if (!call) return true;
        while (end < line.size() && line[end] == ' ') ++end;
        if (end < line.size() && (line[end] == '(' || line[end] == '<')) return true;
    }
    return false;
}

bool top_level(std::string_view line) { return !line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '#' && !line.starts_with("//"); }

std::string_view trimmed(std::string_view line) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.remove_suffix(1);
    return line;
}

}  // namespace

std::optional<std::string_view> embedded_source(std::string_view path) {
    for (std::size_t k = 0; k < kSignalsCardSourcesCount; ++k)
        if (path == kSignalsCardSources[k].name) return std::string_view{kSignalsCardSources[k].text};
    return std::nullopt;
}

std::optional<std::size_t> source_line(std::string_view text, std::string_view function) {
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = text.find('\n', at);
        lines.push_back(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    const auto find = [&](auto&& match) -> std::optional<std::size_t> {
        for (std::size_t k = 0; k < lines.size(); ++k)
            if (match(lines[k])) return k;
        return std::nullopt;
    };
    if (auto k = find([&](std::string_view l) { return top_level(l) && names(l, function, true) && !trimmed(l).ends_with(';'); })) return k;
    if (auto k = find([&](std::string_view l) { return top_level(l) && names(l, function, true); })) return k;
    if (auto k = find([&](std::string_view l) { return top_level(l) && (l.starts_with("class ") || l.starts_with("struct ")) && names(l, function, false); }))
        return k;
    if (auto k = find([&](std::string_view l) { return top_level(l) && names(l, function, false); })) return k;
    return find([&](std::string_view l) { return names(l, function, false); });
}

}  // namespace signals::theory
