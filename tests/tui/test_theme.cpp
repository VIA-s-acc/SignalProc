#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include <ftxui/screen/color.hpp>

#include "tui/theme/theme.hpp"

using namespace signals::tui;

TEST_CASE("builtin themes carry the fixed trace colours", "[tui][theme]") {
    for (auto name : {"dark", "light"}) {
        INFO(name);
        auto t = builtin_theme(name);
        CHECK(t.name == name);
        CHECK(t.trace[1] == Rgb{0xD5, 0x5E, 0x00});
    }
    CHECK(builtin_theme("dark").trace[0] == Rgb{0x56, 0xB4, 0xE9});
    CHECK(builtin_theme("light").trace[0] == Rgb{0x2C, 0x8C, 0xC4});
    CHECK(builtin_theme("dark").accent == Rgb{0xE8, 0xC5, 0x47});
    CHECK(builtin_theme("dark").bg == Rgb{0x0D, 0x11, 0x17});
    CHECK(builtin_theme("nope").name == "dark");
}

TEST_CASE("bad theme files are named errors", "[tui][theme]") {
    auto r = parse_theme("fg = \"#12\"\n");
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("fg") != std::string::npos);
    auto missing = parse_theme("name = \"x\"\n");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("bg") != std::string::npos);
    auto unknown = parse_theme("colour = \"#000000\"\n");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.find("colour") != std::string::npos);
    CHECK_FALSE(parse_theme("this is = = not toml"));
}

TEST_CASE("colour depth fallbacks never emit 24-bit colour", "[tui][theme]") {
    CHECK(to_color({0x56, 0xB4, 0xE9}, ColorDepth::truecolor) == ftxui::Color::RGB(0x56, 0xB4, 0xE9));
    CHECK(to_color({0x56, 0xB4, 0xE9}, ColorDepth::palette256) == ftxui::Color(static_cast<ftxui::Color::Palette256>(74)));
    CHECK(to_color({255, 255, 255}, ColorDepth::ansi16) == ftxui::Color(ftxui::Color::White));
    CHECK(to_color({0, 0, 0}, ColorDepth::none) == ftxui::Color(ftxui::Color::Default));
}

TEST_CASE("viridis ends match matplotlib", "[tui][theme]") {
    CHECK(viridis(0.0) == Rgb{68, 1, 84});
    CHECK(viridis(1.0) == Rgb{253, 231, 37});
    CHECK(viridis(-1.0) == viridis(0.0));
    CHECK(viridis(2.0) == viridis(1.0));
}

TEST_CASE("256-colour lookups are cheap enough for a frame", "[tui][theme]") {
    const auto t = builtin_theme("dark");
    const std::vector<Rgb> colours{t.bg, t.fg, t.dim, t.accent, t.ok, t.warn, t.err, t.trace[0], t.trace[1], t.trace[2]};
    const auto start = std::chrono::steady_clock::now();
    for (int k = 0; k < 5000; ++k) (void)to_color(colours[static_cast<std::size_t>(k) % colours.size()], ColorDepth::palette256);
    const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - start;
    CHECK(took.count() < 50);
    CHECK(to_color(Rgb{0x56, 0xB4, 0xE9}, ColorDepth::palette256) == ftxui::Color(static_cast<ftxui::Color::Palette256>(74)));
}

TEST_CASE("every theme colour reads against its background", "[tui][theme]") {
    auto luminance = [](signals::tui::Rgb c) {
        auto channel = [](std::uint8_t v) {
            const double x = v / 255.0;
            return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
    };
    auto ratio = [&](signals::tui::Rgb a, signals::tui::Rgb b) {
        const double la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    for (const char* name : {"dark", "light"}) {
        INFO(name);
        const auto t = signals::tui::builtin_theme(name);
        CHECK(ratio(t.fg, t.bg) >= 7);
        CHECK(ratio(t.dim, t.bg) >= 4.5);
        for (const auto c : {t.accent, t.ok, t.warn, t.err}) CHECK(ratio(c, t.bg) >= 3);
        for (std::size_t k = 0; k < t.trace.size(); ++k) {
            INFO("trace" << k + 1);
            CHECK(ratio(t.trace[k], t.bg) >= 3);
        }
    }
}
