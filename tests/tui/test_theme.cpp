#include <catch2/catch_test_macros.hpp>

#include <ftxui/screen/color.hpp>

#include "tui/theme/theme.hpp"

using namespace signals::tui;

TEST_CASE("builtin themes carry the fixed trace colours", "[tui][theme]") {
    for (auto name : {"dark", "light"}) {
        INFO(name);
        auto t = builtin_theme(name);
        CHECK(t.name == name);
        CHECK(t.trace[0] == Rgb{0x56, 0xB4, 0xE9});
        CHECK(t.trace[1] == Rgb{0xD5, 0x5E, 0x00});
    }
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
