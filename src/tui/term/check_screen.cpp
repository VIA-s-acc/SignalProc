#include "tui/term/check_screen.hpp"

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {

bool TerminalCheck::on_event(const ftxui::Event& e, UiContext& ui) {
    if (e == ftxui::Event::Escape) {
        close();
        return true;
    }
    if (!e.is_character()) return true;
    const auto c = e.character();
    if (c == "1") ui.caps.color = ColorDepth::none;
    else if (c == "2") ui.caps.color = ColorDepth::ansi16;
    else if (c == "3") ui.caps.color = ColorDepth::palette256;
    else if (c == "4") ui.caps.color = ColorDepth::truecolor;
    else if (c == "u") ui.caps.glyphs = GlyphMode::unicode;
    else if (c == "a") ui.caps.glyphs = GlyphMode::ascii;
    else if (c == "s") {
        if (ui.prefs_file.empty()) {
            status_ = tr("tui.check.nowhere");
        } else {
            auto saved = save_prefs(ui.prefs_file, {ui.caps.color, ui.caps.glyphs, true});
            status_ = saved ? tr("tui.check.saved") + ui.prefs_file.string() : saved.error().message;
        }
    }
    return true;
}

ftxui::Element TerminalCheck::render(const UiContext& ui) const {
    using namespace ftxui;
    Elements gradient;
    for (int k = 0; k < 16; ++k) {
        const Rgb top = viridis(k / 15.0), bottom = viridis(std::min(1.0, (k + 0.5) / 15.0));
        gradient.push_back(text("▀") | color(to_color(top, ui.caps.color)) | bgcolor(to_color(bottom, ui.caps.color)));
    }
    auto option = [&](const std::string& key, const std::string& label, bool on) {
        auto t = text(" " + key + " " + label + " ");
        return on ? t | bold | fg(ui.theme.bg, ui) | bg(ui.theme.accent, ui) : t | fg(ui.theme.fg, ui);
    };
    auto body = vbox({
        text(tr("tui.check.detected") + (ui.caps.emulator.empty() ? std::string{"—"} : ui.caps.emulator)),
        text(""),
        hbox({text(tr("tui.check.braille")), text("⣿⡇⠿⠛⣀⣤⣶")}),
        hbox({text(tr("tui.check.halfblocks")), hbox(std::move(gradient))}),
        hbox({text(tr("tui.check.eighths")), text("▏▎▍▌▋▊▉█ ▁▂▃▄▅▆▇")}),
        hbox({text(tr("tui.check.quadrants")), text("▖▗▘▙▚▛▜▝▞▟")}),
        hbox({text(tr("tui.check.sextants")), text("🬀🬁🬂🬃🬄🬅🬆🬇")}),
        hbox({text(tr("tui.check.octants")), text("𜴀𜴁𜴂𜴃𜴄𜴅")}),
        text(""),
        hbox({text(tr("tui.check.colour")), option("1", tr("tui.check.none"), ui.caps.color == ColorDepth::none),
              option("2", "16", ui.caps.color == ColorDepth::ansi16), option("3", "256", ui.caps.color == ColorDepth::palette256),
              option("4", tr("tui.check.true"), ui.caps.color == ColorDepth::truecolor)}),
        hbox({text(tr("tui.check.glyphs")), option("u", "Unicode", ui.caps.glyphs == GlyphMode::unicode),
              option("a", "ASCII", ui.caps.glyphs == GlyphMode::ascii)}),
        text(""),
        text(tr("tui.check.keys")) | fg(ui.theme.dim, ui),
        text(status_) | fg(ui.theme.ok, ui),
    });
    return panel(tr("tui.check.title"), {}, body, true, ui);
}

}  // namespace signals::tui
