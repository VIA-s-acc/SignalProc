#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "theory/cards.hpp"
#include "theory/live.hpp"
#include "theory/markdown.hpp"
#include "theory/sections.hpp"
#include "theory/text.hpp"
#include "tui/harness.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/document.hpp"

using namespace signals::tui;
namespace theory = signals::theory;

namespace {

std::string card_text(const theory::Card& c) {
    std::string md;
    for (const auto& s : c.sections) md += "## " + s.heading + "\n\n" + s.body + "\n";
    return md;
}

}  // namespace

TEST_CASE("a section renders formulas, tables and links within the width", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    for (const char* id : {"fb-p0-factorisations", "fbm-boundary-modes", "dwt-mallat", "haar-walkthrough"})
        for (int width : {60, 100}) {
            DocumentSpec s{theory::parse_markdown(theory::find_section(id)->body), width};
            DocumentLayout out;
            const auto text = signals::test::render_text(document(s, *ui, &out), width, 400);
            INFO(id << " at " << width);
            CHECK(out.height > 5);
            CHECK(out.lines.size() == static_cast<std::size_t>(out.height));
            for (const auto& line : out.lines) {
                INFO(line);
                CHECK(theory::display_width(line) <= static_cast<std::size_t>(width));
            }
            if (std::string{id} == "fb-p0-factorisations") {
                CHECK_FALSE(out.links.empty());
                CHECK_FALSE(out.formulas.empty());
            }
        }
}

TEST_CASE("unknown latex shows the source and a note", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    DocumentSpec s{theory::parse_markdown("$$a + \\foo{b}$$\n"), 80};
    DocumentLayout out;
    document(s, *ui, &out);
    bool source = false, note = false;
    for (const auto& l : out.lines) {
        source = source || l.find("a + \\foo{b}") != std::string::npos;
        note = note || l.find("формула не разобрана") != std::string::npos;
    }
    CHECK(source);
    CHECK(note);
    for (const auto& l : out.lines) CHECK(l.find("unknown") == std::string::npos);
}

TEST_CASE("missing live values print a dash and never a check mark", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    const auto* card = theory::find_card("dwt");
    REQUIRE(card);
    theory::LiveValues none;
    DocumentSpec s{theory::parse_markdown(card_text(*card)), 100};
    s.values = &none;
    s.section_rule = "─";
    DocumentLayout out;
    document(s, *ui, &out);
    bool no_data = false;
    for (const auto& l : out.lines) {
        CHECK(l.find("✓") == std::string::npos);
        no_data = no_data || l.find("— нет данных") != std::string::npos;
    }
    CHECK(no_data);
    theory::LiveValues v;
    v.set("block.n", "450 686");
    v.set_check({"check.round_trip", "", 1.6e-15, 1e-12});
    s.values = &v;
    document(s, *ui, &out);
    bool tick = false, n = false;
    for (const auto& l : out.lines) {
        tick = tick || l.find("✓") != std::string::npos;
        n = n || l.find("450 686") != std::string::npos;
    }
    CHECK(tick);
    CHECK(n);
}

TEST_CASE("ascii glyphs keep formulas and decorations ascii", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    DocumentSpec s{theory::parse_markdown("## Формулы\n\nтекст $\\omega \\le \\pi$\n\n$$\\sum_n h_0[n] = \\sqrt2$$\n\n- пункт\n\n---\n"), 80};
    s.glyphs = theory::MathGlyphs::ascii;
    s.section_rule = "─";
    DocumentLayout out;
    document(s, *ui, &out);
    std::string all;
    for (const auto& l : out.lines) all += l + "\n";
    CHECK(all.find("omega <= pi") != std::string::npos);
    for (const char* bad : {"ω", "≤", "∑", "√", "─", "•"}) {
        INFO(bad);
        CHECK(all.find(bad) == std::string::npos);
    }
}

TEST_CASE("wide display formulas are clipped with an ellipsis and scroll sideways", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    const std::string wide = "$$" + std::string(60, 'x') + "$$\n";
    DocumentSpec s{theory::parse_markdown(wide), 30};
    DocumentLayout out;
    document(s, *ui, &out);
    REQUIRE_FALSE(out.lines.empty());
    CHECK(out.lines[0].find("…") != std::string::npos);
    s.hscroll = 40;
    document(s, *ui, &out);
    CHECK(theory::display_width(out.lines[0]) <= 30);
}

TEST_CASE("every card renders without unparsed formulas", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    REQUIRE(theory::cards().size() == 32);
    theory::LiveValues values;
    for (const auto& card : theory::cards())
        for (const auto& s : card.sections)
            for (const auto& p : theory::placeholders(s.body)) values.set(p, "12 345");
    for (const auto& card : theory::cards())
        for (const auto* v : {static_cast<const theory::LiveValues*>(nullptr), static_cast<const theory::LiveValues*>(&values)})
            for (auto glyphs : {theory::MathGlyphs::unicode, theory::MathGlyphs::ascii}) {
                DocumentSpec s{theory::parse_markdown(card_text(card)), 100};
                s.values = v;
                s.glyphs = glyphs;
                s.section_rule = "─";
                DocumentLayout out;
                document(s, *ui, &out);
                for (const auto& l : out.lines) {
                    INFO(card.id << ": " << l);
                    CHECK(l.find("формула не разобрана") == std::string::npos);
                    CHECK(theory::display_width(l) <= 100);
                }
            }
}

TEST_CASE("long live values wrap like text instead of being cut", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    theory::LiveValues v;
    v.set("block.band_lengths", "a₆ 7 044 · d₆ 7 044 · d₅ 14 086 · d₄ 28 170 · d₃ 56 338 · d₂ 112 673 · d₁ 225 344");
    DocumentSpec s{theory::parse_markdown("Длины полос: {{block.band_lengths}}; сумма.\n"), 76};
    s.values = &v;
    DocumentLayout out;
    document(s, *ui, &out);
    std::string all;
    for (const auto& l : out.lines) {
        CHECK(theory::display_width(l) <= 76);
        all += l + "\n";
    }
    CHECK(all.find("225 344") != std::string::npos);
    CHECK(all.find("…") == std::string::npos);
}

TEST_CASE("links and formulas outside the visible rows have no box", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    std::string md;
    for (int i = 0; i < 30; ++i) md += "Абзац {#haar} и $x_" + std::to_string(i) + "$.\n\n$$a_" + std::to_string(i) + "$$\n\n";
    DocumentSpec s{theory::parse_markdown(md), 60};
    s.height = 5;
    DocumentLayout out;
    signals::test::render_text(document(s, *ui, &out), 60, 5);
    REQUIRE(out.links.size() == 30);
    CHECK(out.links.front().box.x_max >= out.links.front().box.x_min);
    CHECK(out.links.back().box.x_max < out.links.back().box.x_min);
    REQUIRE(out.formulas.size() == 30);
    CHECK(out.formulas.back().box.x_max < out.formulas.back().box.x_min);
}

TEST_CASE("sideways scroll moves a whole code block and stops at its end", "[tui][document]") {
    auto ui = signals::test::make_ui(150, 44);
    const std::string long_line(100, 'x');
    DocumentSpec s{theory::parse_markdown("```cpp\n    abcdefghijklmnop\n" + long_line + "\n```\n"), 30};
    s.hscroll = 10;
    DocumentLayout out;
    document(s, *ui, &out);
    REQUIRE(out.lines.size() == 2);
    CHECK(out.lines[0].starts_with("ghijklmnop"));
    CHECK(out.lines[0].find("…") == std::string::npos);
    CHECK(out.lines[1].find("…") != std::string::npos);
    CHECK(out.max_hscroll == 70);
    s.hscroll = 1000;
    document(s, *ui, &out);
    CHECK(out.lines[1].find("…") == std::string::npos);
    CHECK(theory::display_width(out.lines[1]) <= 30);
}
