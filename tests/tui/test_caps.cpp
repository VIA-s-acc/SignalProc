#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include "support/paths.hpp"
#include "tui/term/caps.hpp"

using namespace signals::tui;

namespace {

EnvLookup env(std::map<std::string, std::string> m) {
    return [m](std::string_view k) -> std::optional<std::string> {
        auto it = m.find(std::string{k});
        return it == m.end() ? std::nullopt : std::optional{it->second};
    };
}

}  // namespace

TEST_CASE("terminal capabilities follow the environment", "[tui][caps]") {
    CHECK(detect_caps(env({{"COLORTERM", "truecolor"}, {"LANG", "ru_RU.UTF-8"}})).color == ColorDepth::truecolor);
    CHECK(detect_caps(env({{"COLORTERM", "24bit"}})).color == ColorDepth::truecolor);
    CHECK(detect_caps(env({{"TERM", "xterm-256color"}})).color == ColorDepth::palette256);
    CHECK(detect_caps(env({{"TERM_PROGRAM", "Apple_Terminal"}})).color == ColorDepth::palette256);
    CHECK(detect_caps(env({{"TERM", "xterm"}})).color == ColorDepth::ansi16);
    CHECK(detect_caps(env({{"NO_COLOR", "1"}, {"COLORTERM", "truecolor"}})).color == ColorDepth::none);
    auto dumb = detect_caps(env({{"TERM", "dumb"}, {"LANG", "C.UTF-8"}}));
    CHECK(dumb.color == ColorDepth::none);
    CHECK(dumb.glyphs == GlyphMode::ascii);
    CHECK(detect_caps(env({{"LANG", "C"}})).glyphs == GlyphMode::ascii);
    CHECK(detect_caps(env({{"LC_ALL", "en_US.utf8"}})).glyphs == GlyphMode::unicode);
    CHECK(detect_caps(env({{"TERM_PROGRAM", "iTerm.app"}})).emulator == "iTerm.app");
}

TEST_CASE("terminal preferences round trip and override detection", "[tui][caps]") {
    const auto dir = signals::test::temp_path("xdg");
    std::filesystem::remove_all(dir);
    const auto p = prefs_path(env({{"XDG_CONFIG_HOME", dir.string()}}));
    CHECK(p == dir / "signals" / "terminal.toml");
    CHECK(prefs_path(env({{"HOME", "/home/u"}})) == std::filesystem::path{"/home/u/.config/signals/terminal.toml"});
    auto empty = load_prefs(p);
    REQUIRE(empty);
    CHECK_FALSE(empty->checked);
    REQUIRE(save_prefs(p, {ColorDepth::palette256, GlyphMode::ascii, true, std::nullopt, std::nullopt}));
    auto back = load_prefs(p);
    REQUIRE(back);
    CHECK(back->checked);
    CHECK(back->color == ColorDepth::palette256);
    CHECK(back->glyphs == GlyphMode::ascii);
    auto applied = apply({ColorDepth::truecolor, GlyphMode::unicode, ""}, *back);
    CHECK(applied.color == ColorDepth::palette256);
    CHECK(applied.glyphs == GlyphMode::ascii);
    CHECK(apply({ColorDepth::truecolor, GlyphMode::unicode, ""}, {}).color == ColorDepth::truecolor);
    CHECK_FALSE(back->language);
    REQUIRE(save_prefs(p, {ColorDepth::palette256, GlyphMode::ascii, true, signals::Language::en, std::nullopt}));
    back = load_prefs(p);
    TerminalPrefs with_editor{ColorDepth::palette256, GlyphMode::ascii, true, signals::Language::en, std::nullopt};
    with_editor.editor = "code -w";
    REQUIRE(save_prefs(p, with_editor));
    REQUIRE(load_prefs(p));
    CHECK(load_prefs(p)->editor == std::optional<std::string>{"code -w"});
    REQUIRE(back);
    CHECK(back->language == signals::Language::en);
}

TEST_CASE("a broken preferences file is an io error with its path", "[tui][caps]") {
    const auto p = signals::test::temp_path("bad-terminal.toml");
    std::ofstream{p} << "color = = =";
    auto r = load_prefs(p);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == signals::core::Errc::io_error);
    CHECK(r.error().message.find("bad-terminal.toml") != std::string::npos);
}

TEST_CASE("ascii glyphs have single-column replacements", "[tui][caps]") {
    CHECK(glyph(Glyph::tl, GlyphMode::unicode) == "╭");
    CHECK(glyph(Glyph::tl, GlyphMode::ascii) == "+");
    CHECK(glyph(Glyph::h, GlyphMode::ascii) == "-");
    CHECK(glyph(Glyph::v, GlyphMode::ascii) == "|");
    CHECK(glyph(Glyph::check, GlyphMode::ascii) == "v");
    CHECK(glyph(Glyph::cross, GlyphMode::ascii) == "x");
    CHECK(glyph(Glyph::ellipsis, GlyphMode::ascii) == "...");
    CHECK(glyph(Glyph::bullet_on, GlyphMode::unicode) == "◉");
}

TEST_CASE("NO_COLOR wins over saved preferences and must be non-empty", "[tui][caps]") {
    CHECK(detect_caps(env({{"NO_COLOR", ""}, {"COLORTERM", "truecolor"}})).color == ColorDepth::truecolor);
    const auto off = detect_caps(env({{"NO_COLOR", "1"}, {"COLORTERM", "truecolor"}}));
    TerminalPrefs saved;
    saved.color = ColorDepth::truecolor;
    saved.checked = true;
    CHECK(apply(off, saved).color == ColorDepth::none);
}
