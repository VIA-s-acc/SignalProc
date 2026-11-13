#include "theory/markdown.hpp"

#include <cctype>
#include <charconv>
#include <optional>
#include <utility>

#include "theory/sections.hpp"

namespace signals::theory {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const auto end = text.find('\n');
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return out;
}

std::size_t closing_dollar(std::string_view s, std::size_t from, std::string_view delim) {
    for (std::size_t j = from; j < s.size(); ++j) {
        if (s[j] == '\\') {
            ++j;
            continue;
        }
        if (s.compare(j, delim.size(), delim) == 0) return j;
    }
    return std::string_view::npos;
}

struct SectionRef {
    std::string label, target;
    std::size_t length = 0;
    bool resolved = false;
};

std::optional<SectionRef> section_ref(std::string_view s, std::size_t at) {
    static constexpr std::string_view kSign = "§";
    if (s.compare(at, kSign.size(), kSign) != 0) return std::nullopt;
    std::size_t i = at + kSign.size();
    const auto digits_start = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
    if (i == digits_start) return std::nullopt;
    auto number = std::string{s.substr(digits_start, i - digits_start)};
    while (!number.empty() && number.back() == '.') {
        number.pop_back();
        --i;
    }
    if (number.empty()) return std::nullopt;
    static constexpr std::string_view kDash = "–";
    std::size_t end = i;
    if (s.compare(i, kDash.size(), kDash) == 0) {
        std::size_t j = i + kDash.size();
        const auto second = j;
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
        while (j > second && s[j - 1] == '.') --j;
        if (j > second) end = j;
    }
    SectionRef r;
    r.label = std::string{s.substr(at, end - at)};
    r.length = end - at;
    if (const auto* sec = find_section_by_number(number)) {
        r.target = sec->id;
        r.resolved = true;
    }
    return r;
}

std::string ref_label(const Section& s) { return s.number.empty() ? s.title : "§" + s.number; }

void flush(std::vector<Inline>& out, std::string& text) {
    if (text.empty()) return;
    if (!out.empty() && out.back().kind == Inline::Kind::text) out.back().text += text;
    else out.push_back({Inline::Kind::text, std::move(text), {}, {}});
    text.clear();
}

std::vector<Inline> inlines(std::string_view s, std::vector<std::string>* unresolved) {
    std::vector<Inline> out;
    std::string text;
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '$' || s[i + 1] == '*' || s[i + 1] == '`')) {
            text += s[i + 1];
            i += 2;
            continue;
        }
        if (c == '`') {
            const auto close = s.find('`', i + 1);
            if (close != std::string_view::npos) {
                flush(out, text);
                out.push_back({Inline::Kind::code, std::string{s.substr(i + 1, close - i - 1)}, {}, {}});
                i = close + 1;
                continue;
            }
        }
        if (c == '$') {
            const std::string_view delim = s.compare(i, 2, "$$") == 0 ? "$$" : "$";
            const auto close = closing_dollar(s, i + delim.size(), delim);
            if (close != std::string_view::npos) {
                flush(out, text);
                out.push_back({Inline::Kind::math, std::string{s.substr(i + delim.size(), close - i - delim.size())}, {}, {}});
                i = close + delim.size();
                continue;
            }
        }
        if (s.compare(i, 2, "**") == 0) {
            const auto close = s.find("**", i + 2);
            if (close != std::string_view::npos && close > i + 2) {
                flush(out, text);
                out.push_back({Inline::Kind::strong, {}, {}, inlines(s.substr(i + 2, close - i - 2), unresolved)});
                i = close + 2;
                continue;
            }
        }
        if (c == '*' && i + 1 < s.size() && s[i + 1] != ' ' && s[i + 1] != '*') {
            const auto close = s.find('*', i + 1);
            if (close != std::string_view::npos && s[close - 1] != ' ') {
                flush(out, text);
                out.push_back({Inline::Kind::emphasis, {}, {}, inlines(s.substr(i + 1, close - i - 1), unresolved)});
                i = close + 1;
                continue;
            }
        }
        if (s.compare(i, 2, "{{") == 0) {
            const auto close = s.find("}}", i + 2);
            if (close != std::string_view::npos) {
                flush(out, text);
                out.push_back({Inline::Kind::placeholder, std::string{trim(s.substr(i + 2, close - i - 2))}, {}, {}});
                i = close + 2;
                continue;
            }
        }
        if (s.compare(i, 2, "{#") == 0) {
            const auto close = s.find('}', i + 2);
            if (close != std::string_view::npos) {
                const auto id = s.substr(i + 2, close - i - 2);
                if (const auto* sec = find_section(id)) {
                    flush(out, text);
                    out.push_back({Inline::Kind::ref, ref_label(*sec), std::string{id}, {}});
                } else {
                    if (unresolved) unresolved->push_back("{#" + std::string{id} + "}");
                    text += s.substr(i, close + 1 - i);
                }
                i = close + 1;
                continue;
            }
        }
        if (c == '[') {
            const auto close = s.find("](", i + 1);
            const auto end = close == std::string_view::npos ? close : s.find(')', close + 2);
            const auto target = close == std::string_view::npos || end == std::string_view::npos ? std::string_view{} : s.substr(close + 2, end - close - 2);
            const bool external = target.starts_with("view:") || target.starts_with("card:") || target.starts_with("source:") || target.starts_with("task:");
            if (external && s.substr(i + 1, close - i - 1).find('\n') == std::string_view::npos) {
                flush(out, text);
                out.push_back({Inline::Kind::ref, std::string{s.substr(i + 1, close - i - 1)}, std::string{target}, {}});
                i = end + 1;
                continue;
            }
            if (target.starts_with("#") && s.substr(i + 1, close - i - 1).find('\n') == std::string_view::npos) {
                const auto label = s.substr(i + 1, close - i - 1);
                const auto id = target.substr(1);
                if (find_section(id)) {
                    flush(out, text);
                    out.push_back({Inline::Kind::ref, std::string{label}, std::string{id}, {}});
                } else {
                    if (unresolved) unresolved->push_back("#" + std::string{id});
                    text += label;
                }
                i = end + 1;
                continue;
            }
        }
        if (auto r = section_ref(s, i)) {
            if (r->resolved) {
                flush(out, text);
                out.push_back({Inline::Kind::ref, r->label, r->target, {}});
            } else {
                if (unresolved) unresolved->push_back(r->label);
                text += r->label;
            }
            i += r->length;
            continue;
        }
        text += c;
        ++i;
    }
    flush(out, text);
    return out;
}

int heading_level(std::string_view line) {
    int level = 0;
    while (level < static_cast<int>(line.size()) && line[static_cast<std::size_t>(level)] == '#') ++level;
    if (level == 0 || level > 6 || static_cast<std::size_t>(level) >= line.size() || line[static_cast<std::size_t>(level)] != ' ') return 0;
    return level;
}

std::string_view strip_heading_id(std::string_view title) {
    title = trim(title);
    if (!title.empty() && title.back() == '}')
        if (const auto open = title.rfind("{#"); open != std::string_view::npos) return trim(title.substr(0, open));
    return title;
}

std::vector<std::string_view> table_cells(std::string_view line) {
    line = trim(line);
    if (line.starts_with('|')) line.remove_prefix(1);
    if (line.ends_with('|')) line.remove_suffix(1);
    std::vector<std::string_view> out;
    std::size_t start = 0;
    bool math = false, code = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
            continue;
        }
        if (line[i] == '`' && !math) code = !code;
        if (line[i] == '$' && !code) math = !math;
        if (line[i] == '|' && !math && !code) {
            out.push_back(trim(line.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(line.substr(start)));
    return out;
}

bool separator_row(std::string_view line) {
    for (char c : trim(line))
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    return true;
}

struct ListMarker {
    bool numbered = false;
    int number = 0;
    int indent = 0;
    std::size_t text = 0;
};

std::optional<ListMarker> list_marker(std::string_view line) {
    ListMarker m;
    while (static_cast<std::size_t>(m.indent) < line.size() && line[static_cast<std::size_t>(m.indent)] == ' ') ++m.indent;
    const auto rest = line.substr(static_cast<std::size_t>(m.indent));
    if ((rest.starts_with("- ") || rest.starts_with("* ")) && rest.size() > 2) {
        m.text = static_cast<std::size_t>(m.indent) + 2;
        return m;
    }
    std::size_t i = 0;
    while (i < rest.size() && std::isdigit(static_cast<unsigned char>(rest[i]))) ++i;
    if (i > 0 && i + 1 < rest.size() && rest[i] == '.' && rest[i + 1] == ' ') {
        m.numbered = true;
        int number = 0;
        const auto digits = rest.substr(0, std::min<std::size_t>(i, 9));
        std::from_chars(digits.data(), digits.data() + digits.size(), number);
        m.number = number;
        m.text = static_cast<std::size_t>(m.indent) + i + 2;
        return m;
    }
    return std::nullopt;
}

std::vector<Block> parse(std::string_view md, std::vector<std::string>* unresolved) {
    const auto lines = split_lines(md);
    std::vector<Block> out;
    std::string paragraph;
    std::optional<Block> item;
    std::string item_text;
    auto end_paragraph = [&] {
        if (!trim(paragraph).empty()) {
            Block b;
            b.kind = Block::Kind::paragraph;
            b.inlines = inlines(trim(paragraph), unresolved);
            out.push_back(std::move(b));
        }
        paragraph.clear();
    };
    auto end_item = [&] {
        if (item) {
            item->inlines = inlines(trim(item_text), unresolved);
            out.push_back(std::move(*item));
        }
        item.reset();
        item_text.clear();
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto line = lines[i];
        const auto t = trim(line);
        if (t.starts_with("```")) {
            end_paragraph();
            end_item();
            Block b;
            b.kind = Block::Kind::code;
            b.language = std::string{trim(t.substr(3))};
            for (++i; i < lines.size() && !trim(lines[i]).starts_with("```"); ++i) {
                b.text += lines[i];
                b.text += '\n';
            }
            out.push_back(std::move(b));
            continue;
        }
        if (t.empty()) {
            end_paragraph();
            end_item();
            continue;
        }
        if (const int level = heading_level(t); level > 0) {
            end_paragraph();
            end_item();
            Block b;
            b.kind = Block::Kind::heading;
            b.level = level;
            b.inlines = inlines(strip_heading_id(t.substr(static_cast<std::size_t>(level))), unresolved);
            out.push_back(std::move(b));
            continue;
        }
        if (t.size() >= 3 && t.find_first_not_of('-') == std::string_view::npos) {
            end_paragraph();
            end_item();
            Block rule;
            rule.kind = Block::Kind::rule;
            out.push_back(std::move(rule));
            continue;
        }
        if (t.starts_with("$$") && t.size() > 4) {
            const auto close = closing_dollar(t, 2, "$$");
            if (close != std::string_view::npos && trim(t.substr(close + 2)).find_first_not_of(".,;:") == std::string_view::npos) {
                end_paragraph();
                end_item();
                Block b;
                b.kind = Block::Kind::display_math;
                b.text = std::string{trim(t.substr(2, close - 2))};
                out.push_back(std::move(b));
                continue;
            }
        }
        if (t.starts_with('|')) {
            end_paragraph();
            end_item();
            Block b;
            b.kind = Block::Kind::table;
            for (; i < lines.size() && trim(lines[i]).starts_with('|'); ++i) {
                if (separator_row(lines[i])) continue;
                std::vector<std::vector<Inline>> row;
                for (auto cell : table_cells(lines[i])) row.push_back(inlines(cell, unresolved));
                b.table.push_back(std::move(row));
            }
            --i;
            out.push_back(std::move(b));
            continue;
        }
        if (auto m = list_marker(line)) {
            end_paragraph();
            end_item();
            Block b;
            b.kind = m->numbered ? Block::Kind::numbered : Block::Kind::bullet;
            b.number = m->number;
            b.level = m->indent / 2;
            item = std::move(b);
            item_text = std::string{line.substr(m->text)};
            continue;
        }
        if (item) {
            item_text += ' ';
            item_text += t;
            continue;
        }
        if (!paragraph.empty()) paragraph += ' ';
        paragraph += t;
    }
    end_paragraph();
    end_item();
    return out;
}

}  // namespace

std::vector<Block> parse_markdown(std::string_view md) { return parse(md, nullptr); }

std::vector<Inline> parse_inlines(std::string_view text) { return inlines(text, nullptr); }

std::vector<std::string> unresolved_refs(std::string_view md) {
    std::vector<std::string> out;
    parse(md, &out);
    return out;
}

std::vector<std::string> math_spans(std::string_view md) {
    std::vector<std::string> out;
    std::string text;
    bool fence = false;
    for (auto line : split_lines(md)) {
        if (trim(line).starts_with("```")) {
            fence = !fence;
            continue;
        }
        if (fence) continue;
        text += line;
        text += '\n';
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
        const std::string_view delim = text.compare(i, 2, "$$") == 0 ? "$$" : "$";
        const auto close = closing_dollar(text, i + delim.size(), delim);
        if (close == std::string_view::npos) break;
        out.push_back(text.substr(i + delim.size(), close - i - delim.size()));
        i = close + delim.size();
    }
    return out;
}

}  // namespace signals::theory
