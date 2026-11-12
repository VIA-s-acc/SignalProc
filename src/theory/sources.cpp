#include "theory/sources.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
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

constexpr std::array<std::string_view, 71> kKeywords{
    "alignas", "alignof", "and", "asm", "auto", "break", "case", "catch", "class", "concept", "const", "consteval", "constexpr", "constinit",
    "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
    "not", "nullptr", "operator", "or", "override", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return",
    "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef",
    "typeid", "typename", "union", "using", "virtual", "volatile", "while"};

constexpr std::array<std::string_view, 30> kTypes{"void",     "bool",      "char",     "char8_t",  "char16_t", "char32_t", "wchar_t",  "short",
                                                  "int",      "long",      "signed",   "unsigned", "float",    "double",   "size_t",   "ptrdiff_t",
                                                  "int8_t",   "int16_t",   "int32_t",  "int64_t",  "uint8_t",  "uint16_t", "uint32_t", "uint64_t",
                                                  "uintptr_t", "intptr_t", "std",      "string",   "vector",   "span"};

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view word) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = text.find('\n', at);
        lines.push_back(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return lines;
}

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
    const auto lines = split_lines(text);
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

std::vector<Piece> highlight_cpp(std::string_view line, bool& in_block_comment) {
    std::vector<Piece> out;
    const auto add = [&](Piece::Kind kind, std::string_view text) {
        if (text.empty()) return;
        if (!out.empty() && out.back().kind == kind && kind == Piece::Kind::plain) out.back().text += text;
        else out.push_back({kind, std::string{text}});
    };
    std::size_t i = 0;
    if (in_block_comment) {
        const auto end = line.find("*/");
        if (end == std::string_view::npos) {
            add(Piece::Kind::comment, line);
            return out;
        }
        add(Piece::Kind::comment, line.substr(0, end + 2));
        in_block_comment = false;
        i = end + 2;
    }
    const auto first = line.find_first_not_of(" \t");
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            const auto end = line.find_first_not_of(" \t", i);
            add(Piece::Kind::plain, line.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
            i = end == std::string_view::npos ? line.size() : end;
        } else if (line.substr(i).starts_with("//")) {
            add(Piece::Kind::comment, line.substr(i));
            i = line.size();
        } else if (line.substr(i).starts_with("/*")) {
            const auto end = line.find("*/", i + 2);
            if (end == std::string_view::npos) {
                add(Piece::Kind::comment, line.substr(i));
                in_block_comment = true;
                i = line.size();
            } else {
                add(Piece::Kind::comment, line.substr(i, end + 2 - i));
                i = end + 2;
            }
        } else if (c == '#' && i == first) {
            auto end = i + 1;
            while (end < line.size() && word_char(line[end])) ++end;
            add(Piece::Kind::preproc, line.substr(i, end - i));
            i = end;
        } else if (c == '"' || c == '\'' || (c == '<' && first != std::string_view::npos && line.substr(first).starts_with("#include"))) {
            const char close = c == '<' ? '>' : c;
            auto end = i + 1;
            while (end < line.size() && line[end] != close) end += line[end] == '\\' && c != '<' ? 2 : 1;
            end = std::min(end + 1, line.size());
            add(Piece::Kind::string, line.substr(i, end - i));
            i = end;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
            auto end = i + 1;
            const bool hex = line.substr(i).starts_with("0x") || line.substr(i).starts_with("0X");
            while (end < line.size()) {
                const char d = line[end];
                const char before = line[end - 1];
                const bool exponent = (d == '+' || d == '-') && (hex ? (before == 'p' || before == 'P') : (before == 'e' || before == 'E'));
                if (!(word_char(d) || d == '.' || d == '\'' || exponent)) break;
                ++end;
            }
            add(Piece::Kind::number, line.substr(i, end - i));
            i = end;
        } else if (word_char(c)) {
            auto end = i + 1;
            while (end < line.size() && word_char(line[end])) ++end;
            const auto word = line.substr(i, end - i);
            auto next = end;
            while (next < line.size() && line[next] == ' ') ++next;
            Piece::Kind kind = Piece::Kind::plain;
            if (listed(kKeywords, word)) kind = Piece::Kind::keyword;
            else if (listed(kTypes, word)) kind = Piece::Kind::type;
            else if (next < line.size() && line[next] == '(') kind = Piece::Kind::function;
            else if (std::isupper(static_cast<unsigned char>(word[0]))) kind = Piece::Kind::type;
            if (kind == Piece::Kind::plain) add(kind, word);
            else out.push_back({kind, std::string{word}});
            i = end;
        } else {
            add(Piece::Kind::plain, line.substr(i, 1));
            ++i;
        }
    }
    return out;
}

std::optional<std::string> resolve_include(std::string_view from, std::string_view include) {
    const auto rooted = "src/" + std::string{include};
    if (embedded_source(rooted)) return rooted;
    const auto relative = (std::filesystem::path{std::string{from}}.parent_path() / std::string{include}).lexically_normal().generic_string();
    if (embedded_source(relative)) return relative;
    return std::nullopt;
}

namespace {

std::optional<std::string_view> defined_name(std::string_view line) {
    if (!top_level(line) || line.starts_with("namespace") || line.starts_with("using namespace") || line.starts_with("}") || line.starts_with("template")) return std::nullopt;
    for (std::string_view head : {"struct ", "class ", "enum class ", "enum ", "union ", "using "}) {
        if (!line.starts_with(head)) continue;
        auto at = head.size();
        auto end = at;
        while (end < line.size() && word_char(line[end])) ++end;
        if (end == at || (head != "using " && trimmed(line).ends_with(';'))) return std::nullopt;
        return line.substr(at, end - at);
    }
    const auto paren = line.find('(');
    if (paren == std::string_view::npos || paren == 0) return std::nullopt;
    auto end = paren;
    while (end > 0 && line[end - 1] == ' ') --end;
    auto at = end;
    while (at > 0 && word_char(line[at - 1])) --at;
    if (at == end || std::isdigit(static_cast<unsigned char>(line[at]))) return std::nullopt;
    const auto name = line.substr(at, end - at);
    if (listed(kKeywords, name) || listed(kTypes, name)) return std::nullopt;
    return name;
}

const std::map<std::string, std::pair<Definition, bool>, std::less<>>& definitions() {
    static const auto index = [] {
        std::map<std::string, std::pair<Definition, bool>, std::less<>> out;
        for (std::size_t f = 0; f < kSignalsCardSourcesCount; ++f) {
            const auto lines = split_lines(kSignalsCardSources[f].text);
            for (std::size_t k = 0; k < lines.size(); ++k) {
                const auto name = defined_name(lines[k]);
                if (!name) continue;
                const bool body = !trimmed(lines[k]).ends_with(';') || lines[k].starts_with("using ");
                auto it = out.find(*name);
                if (it == out.end()) out.emplace(std::string{*name}, std::pair{Definition{kSignalsCardSources[f].name, k}, body});
                else if (body && !it->second.second) it->second = {Definition{kSignalsCardSources[f].name, k}, true};
            }
        }
        return out;
    }();
    return index;
}

}  // namespace

std::optional<Definition> find_definition(std::string_view name) {
    const auto& index = definitions();
    const auto it = index.find(name);
    if (it == index.end()) return std::nullopt;
    return it->second.first;
}

}  // namespace signals::theory
