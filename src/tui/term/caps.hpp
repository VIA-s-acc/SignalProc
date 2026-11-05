#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

EnvLookup process_env();

enum class GlyphMode { unicode, ascii };

struct TerminalCaps {
    ColorDepth color = ColorDepth::ansi16;
    GlyphMode glyphs = GlyphMode::unicode;
    std::string emulator;
};

TerminalCaps detect_caps(const EnvLookup& env);

struct TerminalPrefs {
    std::optional<ColorDepth> color;
    std::optional<GlyphMode> glyphs;
    bool checked = false;
};

std::filesystem::path prefs_path(const EnvLookup& env);
core::Result<TerminalPrefs> load_prefs(const std::filesystem::path& path);
core::Result<void> save_prefs(const std::filesystem::path& path, const TerminalPrefs& prefs);
TerminalCaps apply(TerminalCaps caps, const TerminalPrefs& prefs);

std::string_view to_string(ColorDepth depth);
std::string_view to_string(GlyphMode mode);

enum class Glyph {
    braille_base, half_upper, tl, tr, bl, br, h, v, tick_down, tick_left,
    check, cross, warn, arrow_down, arrow_up, bullet_on, bullet_off, ellipsis,
};

std::string_view glyph(Glyph g, GlyphMode mode);

}  // namespace signals::tui
