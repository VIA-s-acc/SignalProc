#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/widgets/heatmap.hpp"

using namespace signals::tui;

namespace {

ftxui::Screen draw(const HeatmapSpec& s, ColorDepth depth, GlyphMode glyphs, int w = 1, int h = 1) {
    auto sc = ftxui::Screen::Create(ftxui::Dimension::Fixed(w), ftxui::Dimension::Fixed(h));
    ftxui::Render(sc, heatmap(s, builtin_theme("dark"), depth, glyphs));
    return sc;
}

}  // namespace

TEST_CASE("heatmap packs two pixels per cell", "[tui][heatmap]") {
    const HeatmapSpec s{2, 1, {0.0, 1.0}, 0.0, 1.0};
    auto sc = draw(s, ColorDepth::truecolor, GlyphMode::unicode);
    CHECK(sc.CellAt(0, 0).character == "▀");
    CHECK(sc.CellAt(0, 0).foreground_color == to_color(viridis(0), ColorDepth::truecolor));
    CHECK(sc.CellAt(0, 0).background_color == to_color(viridis(1), ColorDepth::truecolor));
}

TEST_CASE("heatmap resamples to the cell grid", "[tui][heatmap]") {
    const HeatmapSpec s{4, 2, {0, 0, 0, 0, 1, 1, 1, 1}, 0.0, 1.0};
    auto sc = draw(s, ColorDepth::truecolor, GlyphMode::unicode, 4, 1);
    for (int x = 0; x < 4; ++x) {
        CHECK(sc.CellAt(x, 0).foreground_color == to_color(viridis(0), ColorDepth::truecolor));
        CHECK(sc.CellAt(x, 0).background_color == to_color(viridis(1), ColorDepth::truecolor));
    }
}

TEST_CASE("heatmap falls back to shades without colour", "[tui][heatmap]") {
    const HeatmapSpec s{2, 1, {1.0, 1.0}, 0.0, 1.0};
    const std::string shades[] = {" ", "░", "▒", "▓", "█"};
    auto ansi = draw(s, ColorDepth::ansi16, GlyphMode::unicode);
    CHECK(ansi.CellAt(0, 0).character == "█");
    auto none = draw({2, 1, {0.5, 0.5}, 0.0, 1.0}, ColorDepth::none, GlyphMode::unicode);
    CHECK(none.CellAt(0, 0).character == "▒");
    auto ascii = draw(s, ColorDepth::truecolor, GlyphMode::ascii);
    CHECK(ascii.CellAt(0, 0).character == "@");
    auto low = draw({2, 1, {0.0, 0.0}, 0.0, 1.0}, ColorDepth::ansi16, GlyphMode::ascii);
    CHECK(low.CellAt(0, 0).character == " ");
}

TEST_CASE("nan pixels take the background colour", "[tui][heatmap]") {
    const HeatmapSpec s{2, 1, {std::nan(""), 1.0}, 0.0, 1.0};
    auto sc = draw(s, ColorDepth::truecolor, GlyphMode::unicode);
    CHECK(sc.CellAt(0, 0).foreground_color == to_color(builtin_theme("dark").bg, ColorDepth::truecolor));
}
