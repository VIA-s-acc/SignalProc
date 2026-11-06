#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "i18n/i18n.hpp"
#include "tui/widgets/format.hpp"

using namespace signals::tui;

TEST_CASE("numbers use comma, true minus and three digits", "[tui][format]") {
    CHECK(format_number(-0.013326) == "−0,0133");
    CHECK(format_number(1797) == "1 797");
    CHECK(format_number(450'686) == "450 686");
    CHECK(format_number(10.62) == "10,6");
    CHECK(format_number(0.5) == "0,5");
    CHECK(format_number(0) == "0");
    CHECK(format_number(3.1e-16, 2) == "3,1·10⁻¹⁶");
    CHECK(format_number(2.5e7) == "2,5·10⁷");
    CHECK(format_number(std::nan("")) == "—");
    CHECK(format_number(-INFINITY) == "−∞");
    CHECK(format_si(0.01161, "с") == "11,6 мс");
    CHECK(format_si(1378.1, "Гц") == "1,38 кГц");
    CHECK(format_si(190'840'832, "Б") == "182 МБ");
    CHECK(format_si(512, "Б") == "512 Б");
    CHECK(format_si(0, "Гц") == "0 Гц");
}

TEST_CASE("nice ticks cover the range with 1-2-5 steps", "[tui][format]") {
    auto t = nice_ticks(-100, 0, 6);
    REQUIRE(t.size() == 6);
    CHECK(t.front().value == -100);
    CHECK(t.back().value == 0);
    CHECK(t[1].value - t[0].value == 20);
    CHECK(t[0].label == "−100");
    auto f = nice_ticks(0, 1, 5);
    CHECK(f[1].label == "0,2");
    CHECK(nice_ticks(3, 3, 5).size() == 1);
}

TEST_CASE("log and pi ticks", "[tui][format]") {
    auto l = log_ticks(20, 22050, 12);
    CHECK(l.front().label == "20");
    CHECK(std::ranges::any_of(l, [](auto& k) { return k.label == "1 к"; }));
    CHECK(std::ranges::any_of(l, [](auto& k) { return k.label == "20 к"; }));
    CHECK(log_ticks(20, 22050, 4).size() <= 4);
    auto p = pi_ticks(0, std::numbers::pi, 5);
    REQUIRE(p.size() == 5);
    CHECK(p[2].label == "π/2");
    CHECK(p[3].label == "3π/4");
    CHECK(p[4].label == "π");
    auto n = pi_ticks(-4 * std::numbers::pi, 0, 3);
    CHECK(n.front().label == "−4π");
}

TEST_CASE("eighths bar has a fixed width", "[tui][format]") {
    CHECK(eighths_bar(0.5, 4) == "██  ");
    CHECK(eighths_bar(1.0 / 32, 4) == "▏   ");
    CHECK(eighths_bar(2.0, 3) == "███");
    CHECK(eighths_bar(-1.0, 2) == "  ");
    CHECK(eighths_bar(0.6875, 2) == "█▍");
    CHECK(eighths_bar(0.3125, 2) == "▋ ");
}

TEST_CASE("English numbers use a point, comma groups and Latin units", "[tui][format][i18n]") {
    const signals::LanguageScope en{signals::Language::en};
    CHECK(format_number(-0.013326) == "−0.0133");
    CHECK(format_number(450'686) == "450,686");
    CHECK(format_fixed(1234.5, 2) == "1,234.50");
    CHECK(format_si(0.01161, signals::tr("unit.s")) == "11.6 ms");
    CHECK(format_si(1378.1, signals::tr("unit.hz")) == "1.38 kHz");
    CHECK(format_si(190'840'832, signals::tr("unit.byte")) == "182 MB");
    CHECK(format_si(512, signals::tr("unit.byte")) == "512 B");
    const auto ticks = log_ticks(20, 22050, 12);
    CHECK(std::ranges::any_of(ticks, [](const Tick& t) { return t.label == "20 k"; }));
}

TEST_CASE("Russian units come from the dictionary too", "[tui][format][i18n]") {
    CHECK(format_si(1378.1, signals::tr("unit.hz")) == "1,38 кГц");
    CHECK(format_si(190'840'832, signals::tr("unit.byte")) == "182 МБ");
}
