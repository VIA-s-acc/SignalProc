#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/harness.hpp"
#include "tui/widgets/plot.hpp"

using namespace signals::tui;

namespace {

std::vector<std::string> rows(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss{text};
    for (std::string line; std::getline(ss, line);) out.push_back(line);
    return out;
}

std::vector<char32_t> decode(const std::string& s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

bool is_braille(char32_t c) { return c >= 0x2800 && c <= 0x28FF; }

int dot_count(const std::string& text) {
    int n = 0;
    for (char32_t c : decode(text))
        if (is_braille(c)) n += std::popcount(static_cast<unsigned>(c - 0x2800));
    return n;
}

const Theme& dark() {
    static const Theme t = builtin_theme("dark");
    return t;
}

PlotSpec flat(LineStyle style) {
    PlotSpec s{{0, 1}, {-1, 1}, {Trace{{0, 0}, {}, {255, 255, 255}, style}}};
    s.labels = false;
    return s;
}

}  // namespace

TEST_CASE("a flat line fills one braille row", "[tui][plot]") {
    const auto t = signals::test::render_text(plot(flat(LineStyle::solid), dark(), ColorDepth::truecolor, GlyphMode::unicode), 10, 4);
    int filled = 0;
    for (const auto& r : rows(t)) {
        const auto cps = decode(r);
        if (cps.empty()) continue;
        ++filled;
        CHECK(cps.size() == 10);
        for (char32_t c : cps) CHECK(is_braille(c));
    }
    CHECK(filled == 1);
}

TEST_CASE("the focused trace owns shared cells", "[tui][plot]") {
    const Rgb a{10, 200, 10}, b{200, 10, 10};
    const Trace ta{{0, 0.1}, {}, a}, tb{{0, 0.1}, {}, b, LineStyle::solid, true};
    auto draw = [](std::vector<Trace> traces) {
        PlotSpec s{{0, 1}, {-1, 1}, std::move(traces)};
        s.labels = false;
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(10), ftxui::Dimension::Fixed(4));
        ftxui::Render(screen, plot(s, dark(), ColorDepth::truecolor, GlyphMode::unicode));
        return screen;
    };
    auto only_a = draw({ta}), only_b = draw({tb}), both = draw({tb, ta});
    auto used = [](const ftxui::Screen& sc, int x, int y) { const auto& c = sc.CellAt(x, y).character; return !c.empty() && c != " "; };
    int shared = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 10; ++x)
            if (used(only_a, x, y) && used(only_b, x, y)) {
                ++shared;
                CHECK(both.CellAt(x, y).foreground_color == to_color(b, ColorDepth::truecolor));
            }
    CHECK(shared > 0);
}

TEST_CASE("dashed and dotted styles skip dots", "[tui][plot]") {
    auto count = [](LineStyle st) {
        return dot_count(signals::test::render_text(plot(flat(st), dark(), ColorDepth::truecolor, GlyphMode::unicode), 8, 4));
    };
    CHECK(count(LineStyle::solid) == 16);
    CHECK(count(LineStyle::dashed) == 8);
    CHECK(count(LineStyle::dotted) == 4);
}

TEST_CASE("log axis maps columns and values both ways", "[tui][plot]") {
    Axis a{20, 20000, AxisScale::log10};
    CHECK(column_of_value(a, 20, 100) == 0);
    CHECK(column_of_value(a, 20000, 100) == 99);
    CHECK(std::abs(value_at_column(a, column_of_value(a, 1000, 100), 100) / 1000 - 1) < 0.08);
    CHECK(column_of_value(a, 1e9, 100) == 99);
    CHECK(column_of_value(a, -5, 100) == 0);
    CHECK(column_of_value(a, std::nan(""), 100) == 0);
    Axis lin{0, 10};
    CHECK(column_of_value(lin, 5, 10) == 5);
    CHECK(value_at_column(lin, 0, 10) == 0.5);
}

TEST_CASE("labelled plots show ticks and cursors", "[tui][plot]") {
    PlotSpec s{{0, 1}, {-100, 0}, {Trace{{-50, -50}, {}, {255, 255, 255}}}};
    s.cursor_a = 0.5;
    const auto t = signals::test::render_text(plot(s, dark(), ColorDepth::truecolor, GlyphMode::unicode), 40, 10);
    CHECK(t.find("−100") != std::string::npos);
    CHECK(t.find("│") != std::string::npos);
}

TEST_CASE("small plots degrade to a sparkline", "[tui][plot]") {
    PlotSpec s{{0, 1}, {0, 1}, {Trace{{0, 0.25, 0.5, 1.0}, {}, {255, 255, 255}}}};
    const auto t = signals::test::render_text(plot(s, dark(), ColorDepth::truecolor, GlyphMode::unicode), 30, 2);
    const std::u32string eighths = U"▁▂▃▄▅▆▇█ ";
    bool any = false;
    for (char32_t c : decode(t)) {
        if (c == U'\n') continue;
        CHECK(eighths.find(c) != std::u32string::npos);
        any = any || c != U' ';
    }
    CHECK(any);
}

TEST_CASE("ascii mode draws with asterisks", "[tui][plot]") {
    const auto t = signals::test::render_text(plot(flat(LineStyle::solid), dark(), ColorDepth::none, GlyphMode::ascii), 10, 4);
    CHECK(t.find('*') != std::string::npos);
    for (char32_t c : decode(t)) CHECK_FALSE(is_braille(c));
}
