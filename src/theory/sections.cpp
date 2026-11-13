#include "theory/sections.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "i18n/i18n.hpp"
#include "theory/text.hpp"

extern const char kSignalsTheoryBook[];
extern const char kSignalsTheoryBookEn[];
extern const char kSignalsSigFormat[];
extern const char kSignalsSigFormatEn[];
extern const char kSignalsSettings[];
extern const char kSignalsSettingsEn[];
extern const char kSignalsLearn[];
extern const char kSignalsLearnEn[];

namespace signals::theory {
namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string{s};
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

int heading_level(std::string_view line) {
    int level = 0;
    while (level < static_cast<int>(line.size()) && line[static_cast<std::size_t>(level)] == '#') ++level;
    if (level == 0 || level > 6 || static_cast<std::size_t>(level) >= line.size() || line[static_cast<std::size_t>(level)] != ' ') return 0;
    return level;
}

Section parse_heading(std::string_view line, int level) {
    Section s;
    s.level = level;
    auto rest = trim(line.substr(static_cast<std::size_t>(level)));
    if (const auto open = rest.rfind("{#"); open != std::string::npos && rest.back() == '}') {
        s.id = rest.substr(open + 2, rest.size() - open - 3);
        rest = trim(rest.substr(0, open));
    }
    if (!rest.empty() && rest.back() == ']') {
        if (const auto open = rest.rfind('['); open != std::string::npos) {
            s.tier = trim(rest.substr(open + 1, rest.size() - open - 2));
            rest = trim(rest.substr(0, open));
        }
    }
    std::size_t i = 0;
    while (i < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[i])) || rest[i] == '.')) ++i;
    if (i > 0 && i < rest.size() && rest[i] == ' ' && rest[i - 1] == '.') {
        s.number = rest.substr(0, i - 1);
        rest = trim(rest.substr(i));
    }
    s.title = rest;
    return s;
}

std::vector<Section> parse_book(std::string_view text) {
    const auto lines = split_lines(text);
    std::vector<Section> out;
    std::vector<std::size_t> starts;
    bool fence = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].starts_with("```")) fence = !fence;
        if (fence) continue;
        if (const int level = heading_level(lines[i]); level > 0) {
            out.push_back(parse_heading(lines[i], level));
            starts.push_back(i);
        }
    }
    std::vector<std::size_t> stack;
    for (std::size_t k = 0; k < out.size(); ++k) {
        while (!stack.empty() && out[stack.back()].level >= out[k].level) stack.pop_back();
        if (!stack.empty()) {
            out[k].parent = stack.back();
            if (out[k].tier.empty()) out[k].tier = out[stack.back()].tier;
        }
        stack.push_back(k);
        std::size_t end = lines.size();
        for (std::size_t j = k + 1; j < out.size(); ++j)
            if (out[j].level <= out[k].level) {
                end = starts[j];
                break;
            }
        std::string body;
        for (std::size_t i = starts[k] + 1; i < end; ++i) {
            body += lines[i];
            body += '\n';
        }
        out[k].body = std::move(body);
    }
    return out;
}

}  // namespace

const std::vector<Section>& sections() {
    if (language() == Language::en) {
        static const std::vector<Section> en = parse_book(std::string{kSignalsTheoryBookEn} + "\n" + kSignalsSigFormatEn + "\n" + kSignalsSettingsEn + "\n" + kSignalsLearnEn);
        return en;
    }
    static const std::vector<Section> ru = parse_book(std::string{kSignalsTheoryBook} + "\n" + kSignalsSigFormat + "\n" + kSignalsSettings + "\n" + kSignalsLearn);
    return ru;
}

const Section* find_section(std::string_view id) {
    for (const auto& s : sections())
        if (s.id == id) return &s;
    return nullptr;
}

const Section* find_section_by_number(std::string_view number) {
    for (const auto& s : sections())
        if (!s.number.empty() && s.number == number) return &s;
    return nullptr;
}

std::string_view own_body(const Section& s) {
    std::string_view body = s.body;
    bool fence = false;
    for (std::size_t at = 0; at < body.size();) {
        if (body.compare(at, 3, "```") == 0) fence = !fence;
        if (!fence && body[at] == '#' && (at == 0 || body[at - 1] == '\n')) return body.substr(0, at);
        const auto next = body.find('\n', at);
        at = next == std::string_view::npos ? body.size() : next + 1;
    }
    return body;
}

std::vector<std::size_t> children_of(std::size_t index) {
    std::vector<std::size_t> out;
    const auto& all = sections();
    for (std::size_t i = index + 1; i < all.size(); ++i)
        if (all[i].parent && *all[i].parent == index) out.push_back(i);
    return out;
}

std::size_t index_of(const Section& s) { return static_cast<std::size_t>(&s - sections().data()); }

std::string search_key(std::string_view text) {
    static constexpr std::u32string_view kSub = U"₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎ₐₑₒₓₕₖₗₘₙₚₛₜᵢⱼ";
    static constexpr std::u32string_view kSubPlain = U"0123456789+-=()aeoxhklmnpstij";
    static constexpr std::u32string_view kSup = U"⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁱⁿ";
    static constexpr std::u32string_view kSupPlain = U"0123456789+-=()in";
    std::u32string out;
    for (char32_t c : to_u32(fold_case(text))) {
        if (c == U'_' || c == U'^' || c == U'{' || c == U'}' || c == U'\\' || c == U'$') continue;
        if (const auto i = kSub.find(c); i != std::u32string_view::npos) c = kSubPlain[i];
        else if (const auto j = kSup.find(c); j != std::u32string_view::npos) c = kSupPlain[j];
        else if (c == U'−') c = U'-';
        out.push_back(c);
    }
    return to_utf8(out);
}

std::vector<std::size_t> search_sections(std::string_view query) {
    const auto q = search_key(trim(query));
    if (q.empty()) return {};
    static const auto folded = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& s : sections()) out.emplace_back(search_key(s.title), search_key(own_body(s)));
        return out;
    }();
    std::vector<std::size_t> titles, bodies;
    for (std::size_t i = 0; i < folded.size(); ++i) {
        if (folded[i].first.find(q) != std::string::npos) titles.push_back(i);
        else if (folded[i].second.find(q) != std::string::npos) bodies.push_back(i);
    }
    titles.insert(titles.end(), bodies.begin(), bodies.end());
    return titles;
}

}  // namespace signals::theory
