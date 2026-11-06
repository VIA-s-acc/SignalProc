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
