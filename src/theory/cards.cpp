#include "theory/cards.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <toml++/toml.hpp>

#include "i18n/i18n.hpp"
#include "theory/blocks.hpp"
#include "theory/latex.hpp"
#include "theory/live.hpp"
#include "theory/markdown.hpp"
#include "theory/sections.hpp"

struct SignalsEmbeddedFile {
    const char* name;
    const char* text;
};
extern const SignalsEmbeddedFile kSignalsTheoryCards[];
extern const std::size_t kSignalsTheoryCardsCount;

namespace signals::theory {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

std::vector<std::string> strings(const toml::table& t, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* a = t[key].as_array())
        for (const auto& v : *a)
            if (auto s = v.value<std::string>()) out.push_back(*s);
    return out;
}

std::vector<std::string> normalised_lines(std::string_view text) {
    std::vector<std::string> out;
    std::istringstream in{std::string{text}};
    for (std::string line; std::getline(in, line);) {
        auto t = trim(line);
        if (!t.empty()) out.emplace_back(t);
    }
    return out;
}

bool contains_lines(const std::vector<std::string>& haystack, const std::vector<std::string>& needle) {
    if (needle.empty()) return true;
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

const CardSection* Card::section(std::string_view heading) const {
    for (const auto& s : sections)
        if (s.heading == heading) return &s;
    return nullptr;
}

core::Result<Card> parse_card(std::string_view text) {
    text = trim(text);
    if (!text.starts_with("+++")) return core::make_error(core::Errc::invalid_argument, "card: missing +++ header");
    const auto end = text.find("\n+++", 3);
    if (end == std::string_view::npos) return core::make_error(core::Errc::invalid_argument, "card: unterminated +++ header");
    Card c;
    try {
        const auto header = toml::parse(text.substr(3, end - 3));
        c.id = header["id"].value_or(std::string{});
        c.title = header["title"].value_or(std::string{});
        c.theory = strings(header, "theory");
        c.source = header["source"].value_or(std::string{});
        c.function = header["function"].value_or(std::string{});
        c.test = header["test"].value_or(std::string{});
        c.views = strings(header, "views");
    } catch (const toml::parse_error& e) {
        return core::make_error(core::Errc::invalid_argument, std::string{"card header: "} + std::string{e.description()});
    }
    if (c.id.empty()) return core::make_error(core::Errc::invalid_argument, "card: header has no id");
    std::istringstream in{std::string{text.substr(end + 4)}};
    bool fence = false;
    for (std::string line; std::getline(in, line);) {
        if (line.starts_with("```")) fence = !fence;
        if (!fence && line.starts_with("## ")) {
            c.sections.push_back({std::string{trim(std::string_view{line}.substr(3))}, {}});
            continue;
        }
        if (c.sections.empty()) continue;
        c.sections.back().body += line;
        c.sections.back().body += '\n';
    }
    return c;
}

const std::vector<Card>& cards() {
    static const std::vector<Card> all = [] {
        std::vector<Card> out;
        for (std::size_t i = 0; i < kSignalsTheoryCardsCount; ++i)
            if (auto c = parse_card(kSignalsTheoryCards[i].text)) out.push_back(std::move(*c));
        return out;
    }();
    return all;
}

const Card* find_card(std::string_view id) {
    for (const auto& c : cards())
        if (c.id == id) return &c;
    return nullptr;
}

std::vector<std::string> required_card_sections() {
    std::vector<std::string> out;
    for (const char* key : {"theory.card.purpose", "theory.card.formulas", "theory.card.algorithm", "theory.card.numbers", "theory.card.checks",
                            "theory.card.links", "theory.card.implementation"})
        out.push_back(tr(key));
    return out;
}

std::vector<std::string_view> known_views() {
    return {"signal", "filters", "designer", "decomposition", "matrix", "explain", "under_the_hood", "tutorial"};
}

std::vector<CardIssue> lint_card(const Card& card, const std::filesystem::path& root) {
    std::vector<CardIssue> out;
    auto issue = [&](std::string m) { out.push_back({card.id, std::move(m)}); };
    const auto* block = find_block(card.id);
    if (!block) issue("no block in the catalogue");
    const auto required = required_card_sections();
    std::size_t at = 0;
    for (const auto& name : required) {
        const auto it = std::find_if(card.sections.begin() + static_cast<std::ptrdiff_t>(std::min(at, card.sections.size())), card.sections.end(),
                                     [&](const CardSection& s) { return s.heading == name; });
        if (it == card.sections.end()) {
            issue("missing or misplaced section \"" + name + "\"");
            continue;
        }
        at = static_cast<std::size_t>(it - card.sections.begin()) + 1;
    }
    for (const auto& s : card.sections) {
        if (block)
            for (const auto& p : placeholders(s.body))
                if (std::find(block->values.begin(), block->values.end(), p) == block->values.end())
                    issue("placeholder {{" + p + "}} is not a value of block " + std::string{block->id});
        for (const auto& r : unresolved_refs(s.body)) issue("unresolved reference " + r + " in \"" + s.heading + "\"");
        for (const auto& f : math_spans(s.body))
            if (auto parsed = parse_latex(f); !parsed) issue("formula does not parse: " + parsed.error().message + " in $" + f + "$");
    }
    for (const auto& t : card.theory)
        if (!find_section(t)) issue("theory link {#" + t + "} does not resolve");
    if (card.source.empty() || !std::filesystem::exists(root / card.source)) {
        issue("source " + card.source + " does not exist");
    } else {
        const auto source = read_file(root / card.source);
        if (card.function.empty() || source.find(card.function) == std::string::npos)
            issue("function " + card.function + " is not in " + card.source);
        if (const auto* impl = card.section(tr("theory.card.implementation"))) {
            const auto lines = normalised_lines(source);
            for (const auto& b : parse_markdown(impl->body))
                if (b.kind == Block::Kind::code && !contains_lines(lines, normalised_lines(b.text)))
                    issue("code in \"" + impl->heading + "\" is not in " + card.source + " verbatim");
        }
    }
    if (card.test.empty() || !std::filesystem::exists(root / card.test)) issue("test " + card.test + " does not exist");
    const auto views = known_views();
    for (const auto& v : card.views)
        if (std::find(views.begin(), views.end(), v) == views.end()) issue("unknown view " + v);
    return out;
}

std::vector<CardIssue> lint_cards(const std::filesystem::path& root) {
    std::vector<CardIssue> out;
    if (cards().size() != kSignalsTheoryCardsCount) out.push_back({"*", "some card files do not parse"});
    for (const auto& b : block_catalog())
        if (b.tier == Tier::core && !find_card(b.id)) out.push_back({std::string{b.id}, "core block has no card"});
    for (const auto& c : cards()) {
        auto issues = lint_card(c, root);
        out.insert(out.end(), issues.begin(), issues.end());
    }
    return out;
}

}  // namespace signals::theory
