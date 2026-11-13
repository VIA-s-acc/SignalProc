#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "tui/render/svg_frame.hpp"

using namespace signals::tui;

namespace {

ftxui::Screen grid(int w, int h) {
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
    auto s = ftxui::Screen::Create(ftxui::Dimension::Fixed(w), ftxui::Dimension::Fixed(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto& c = s.CellAt(x, y);
            c.character = " ";
            c.foreground_color = ftxui::Color::RGB(200, 200, 200);
            c.background_color = ftxui::Color::RGB(10, 10, 10);
        }
    return s;
}

ftxui::Cell& put(ftxui::Screen& s, int x, int y, std::string ch) {
    auto& c = s.CellAt(x, y);
    c.character = std::move(ch);
    return c;
}

std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (auto at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) ++n;
    return n;
}

SvgStyle style() {
    SvgStyle s;
    s.fg = {200, 200, 200};
    s.bg = {10, 10, 10};
    return s;
}

}  // namespace

TEST_CASE("to_rgb reads true colour and palette colours", "[svg-frame]") {
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
    const auto rgb = to_rgb(ftxui::Color::RGB(1, 2, 3));
    REQUIRE(rgb);
    CHECK(*rgb == Rgb{1, 2, 3});
    CHECK(to_rgb(ftxui::Color::Red));
    CHECK(to_rgb(ftxui::Color::Palette256(202)));
    CHECK_FALSE(to_rgb(ftxui::Color::Default));
}

TEST_CASE("svg still sizes the canvas from the grid", "[svg-frame]") {
    const auto svg = svg_still(grid(10, 3), style());
    CHECK(svg.starts_with("<svg"));
    CHECK(svg.find("width=\"112\"") != std::string::npos);
    CHECK(svg.find("height=\"79\"") != std::string::npos);
}

TEST_CASE("svg still escapes markup in cell text", "[svg-frame]") {
    auto s = grid(3, 1);
    put(s, 0, 0, "<");
    put(s, 1, 0, "&");
    put(s, 2, 0, "\"");
    const auto svg = svg_still(s, style());
    CHECK(svg.find("&lt;&amp;&quot;") != std::string::npos);
    CHECK(svg.find("<&") == std::string::npos);
}

TEST_CASE("adjacent cells of one style share a text run", "[svg-frame]") {
    auto s = grid(5, 1);
    put(s, 0, 0, "a");
    put(s, 1, 0, "b");
    put(s, 2, 0, "c");
    CHECK(count(svg_still(s, style()), "<text") == 1);
    put(s, 2, 0, "c").foreground_color = ftxui::Color::RGB(255, 0, 0);
    CHECK(count(svg_still(s, style()), "<text") == 2);
}

TEST_CASE("every glyph gets its own x position", "[svg-frame]") {
    auto s = grid(5, 1);
    put(s, 0, 0, "a");
    put(s, 1, 0, "b");
    put(s, 2, 0, "c");
    CHECK(svg_still(s, style()).find("x=\"14 22.4 30.8\"") != std::string::npos);
}

TEST_CASE("empty continuation cells are skipped", "[svg-frame]") {
    auto s = grid(5, 1);
    put(s, 0, 0, "a");
    put(s, 1, 0, "");
    put(s, 2, 0, "b");
    const auto svg = svg_still(s, style());
    CHECK(svg.find("x=\"14 30.8\"") != std::string::npos);
    CHECK(svg.find(">ab<") != std::string::npos);
}

TEST_CASE("bold, dim, underline and inverse map to svg attributes", "[svg-frame]") {
    auto s = grid(4, 1);
    put(s, 0, 0, "b").bold = true;
    put(s, 1, 0, "d").dim = true;
    put(s, 2, 0, "u").underlined = true;
    auto& inv = put(s, 3, 0, "i");
    inv.inverted = true;
    inv.foreground_color = ftxui::Color::RGB(255, 0, 0);
    const auto svg = svg_still(s, style());
    CHECK(svg.find("font-weight=\"bold\"") != std::string::npos);
    CHECK(svg.find("opacity=\"0.6\"") != std::string::npos);
    CHECK(svg.find("text-decoration=\"underline\"") != std::string::npos);
    CHECK(svg.find("<rect x=\"39.2\" y=\"14\" width=\"8.4\" height=\"17\" fill=\"#ff0000\"/>") != std::string::npos);
}

TEST_CASE("background runs become one rect", "[svg-frame]") {
    auto s = grid(8, 1);
    for (int x = 1; x < 6; ++x) put(s, x, 0, " ").background_color = ftxui::Color::RGB(50, 60, 70);
    const auto svg = svg_still(s, style());
    CHECK(count(svg, "fill=\"#323c46\"") == 1);
    CHECK(svg.find("width=\"42\"") != std::string::npos);
}

TEST_CASE("animation shows every frame for its duration", "[svg-frame]") {
    auto a = grid(4, 2), b = grid(4, 2), c = grid(4, 2);
    put(a, 0, 0, "a");
    put(b, 0, 0, "b");
    put(c, 0, 0, "c");
    const std::vector<SvgFrame> frames{{a, 1.0}, {b, 2.0}, {c, 1.0}};
    const auto svg = svg_animation(frames, style());
    CHECK(count(svg, "<g class=\"f") == 3);
    CHECK(svg.find("4s") != std::string::npos);
    CHECK(svg.find("infinite") != std::string::npos);
    CHECK(svg.find("@keyframes") != std::string::npos);
}

TEST_CASE("animation defines repeated rows once", "[svg-frame]") {
    auto a = grid(4, 3);
    put(a, 0, 0, "a");
    put(a, 0, 1, "b");
    put(a, 0, 2, "c");
    const std::vector<SvgFrame> frames{{a, 1.0}, {a, 1.0}, {a, 1.0}};
    const auto svg = svg_animation(frames, style());
    CHECK(count(svg, "<g id=\"r") == 3);
    CHECK(count(svg, "<use href=\"#r") == 9);
}

TEST_CASE("a glyph outside the basic plane gets its own text element", "[svg-frame]") {
    auto s = grid(4, 1);
    put(s, 0, 0, "a");
    put(s, 1, 0, "\xF0\x9F\xAC\x80");
    put(s, 2, 0, "b");
    const auto svg = svg_still(s, style());
    CHECK(count(svg, "<text") == 3);
    CHECK(svg.find("x=\"30.8\"") != std::string::npos);
}
