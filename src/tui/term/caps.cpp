#include "tui/term/caps.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

namespace signals::tui {
namespace {

using core::Errc;
using core::make_error;

std::optional<std::string> non_empty(const EnvLookup& env, std::string_view key) {
    auto v = env(key);
    if (v && !v->empty()) return v;
    return std::nullopt;
}

bool contains_utf8(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find("utf-8") != std::string::npos || s.find("utf8") != std::string::npos;
}

std::optional<ColorDepth> parse_depth(std::string_view s) {
    for (auto d : {ColorDepth::none, ColorDepth::ansi16, ColorDepth::palette256, ColorDepth::truecolor})
        if (to_string(d) == s) return d;
    return std::nullopt;
}

std::optional<GlyphMode> parse_glyphs(std::string_view s) {
    for (auto g : {GlyphMode::unicode, GlyphMode::ascii})
        if (to_string(g) == s) return g;
    return std::nullopt;
}

struct GlyphPair {
    std::string_view unicode, ascii;
};

constexpr std::array<GlyphPair, 18> kGlyphs{{
    {"⠀", " "}, {"▀", "#"}, {"╭", "+"}, {"╮", "+"}, {"╰", "+"}, {"╯", "+"}, {"─", "-"}, {"│", "|"}, {"┬", "+"},
    {"┤", "+"}, {"✓", "v"}, {"✗", "x"}, {"⚠", "!"}, {"↓", "v"}, {"↑", "^"}, {"◉", "*"}, {"○", "o"}, {"…", "..."},
}};

}  // namespace

EnvLookup process_env() {
    return [](std::string_view key) -> std::optional<std::string> {
        const char* v = std::getenv(std::string{key}.c_str());
        return v ? std::optional<std::string>{v} : std::nullopt;
    };
}

TerminalCaps detect_caps(const EnvLookup& env) {
    TerminalCaps caps;
    caps.emulator = non_empty(env, "TERM_PROGRAM").value_or("");
    const auto term = non_empty(env, "TERM").value_or("");
    const auto colorterm = non_empty(env, "COLORTERM").value_or("");
    caps.no_color = non_empty(env, "NO_COLOR").has_value();
    const bool windows_terminal = non_empty(env, "WT_SESSION").has_value();
    if (caps.no_color) caps.color = ColorDepth::none;
    else if (term == "dumb") caps.color = ColorDepth::none;
    else if (colorterm == "truecolor" || colorterm == "24bit" || windows_terminal) caps.color = ColorDepth::truecolor;
    else if (term.find("256color") != std::string::npos || caps.emulator == "Apple_Terminal") caps.color = ColorDepth::palette256;
    else caps.color = ColorDepth::ansi16;
    std::optional<std::string> locale;
    for (auto key : {"LC_ALL", "LC_CTYPE", "LANG"})
        if ((locale = non_empty(env, key))) break;
    caps.glyphs = term != "dumb" && ((locale && contains_utf8(*locale)) || windows_terminal) ? GlyphMode::unicode : GlyphMode::ascii;
    return caps;
}

std::filesystem::path prefs_path(const EnvLookup& env) {
    if (auto xdg = non_empty(env, "XDG_CONFIG_HOME")) return std::filesystem::path{*xdg} / "signals" / "terminal.toml";
#ifdef _WIN32
    if (auto appdata = non_empty(env, "APPDATA")) return std::filesystem::path{*appdata} / "signals" / "terminal.toml";
#endif
    return std::filesystem::path{non_empty(env, "HOME").value_or(".")} / ".config" / "signals" / "terminal.toml";
}

core::Result<TerminalPrefs> load_prefs(const std::filesystem::path& path) {
    TerminalPrefs prefs;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return prefs;
    toml::table table;
    try {
        table = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        return make_error(Errc::io_error, "cannot parse " + path.string() + ": " + std::string{e.description()});
    }
    prefs.checked = table["checked"].value_or(false);
    if (auto c = table["color"].value<std::string>()) prefs.color = parse_depth(*c);
    if (auto g = table["glyphs"].value<std::string>()) prefs.glyphs = parse_glyphs(*g);
    if (auto l = table["language"].value<std::string>()) prefs.language = parse_language(*l);
    if (auto e = table["editor"].value<std::string>(); e && !e->empty()) prefs.editor = *e;
    return prefs;
}

core::Result<void> save_prefs(const std::filesystem::path& path, const TerminalPrefs& prefs) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    toml::table table;
    table.insert("checked", prefs.checked);
    if (prefs.color) table.insert("color", std::string{to_string(*prefs.color)});
    if (prefs.glyphs) table.insert("glyphs", std::string{to_string(*prefs.glyphs)});
    if (prefs.language) table.insert("language", std::string{language_code(*prefs.language)});
    if (prefs.editor) table.insert("editor", *prefs.editor);
    std::ofstream out{path};
    out << table << '\n';
    if (!out) return make_error(Errc::io_error, "cannot write " + path.string());
    return {};
}

TerminalCaps apply(TerminalCaps caps, const TerminalPrefs& prefs) {
    if (prefs.color && !caps.no_color) caps.color = *prefs.color;
    if (prefs.glyphs) caps.glyphs = *prefs.glyphs;
    return caps;
}

std::string_view to_string(ColorDepth depth) {
    switch (depth) {
    case ColorDepth::none: return "none";
    case ColorDepth::ansi16: return "ansi16";
    case ColorDepth::palette256: return "palette256";
    case ColorDepth::truecolor: return "truecolor";
    }
    return "ansi16";
}

std::string_view to_string(GlyphMode mode) { return mode == GlyphMode::ascii ? "ascii" : "unicode"; }

std::string_view glyph(Glyph g, GlyphMode mode) {
    const auto& p = kGlyphs[static_cast<std::size_t>(g)];
    return mode == GlyphMode::ascii ? p.ascii : p.unicode;
}

}  // namespace signals::tui
