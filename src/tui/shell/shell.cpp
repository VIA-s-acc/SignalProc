#include "tui/shell/shell.hpp"

#include <fmt/format.h>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "i18n/i18n.hpp"
#include "tui/monitor/resources_panel.hpp"
#include "tui/shell/palette.hpp"
#include "tui/shell/panel.hpp"
#include "tui/term/check_screen.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

class ShellComponent final : public ftxui::ComponentBase {
public:
    ShellComponent(UiContext& ui, std::vector<std::shared_ptr<TabScreen>> screens, std::function<void()> on_quit)
        : ui_{ui}, screens_{std::move(screens)}, on_quit_{std::move(on_quit)}, tab_boxes_(screens_.size()) {
        if (ui_.first_run_check) check_.open();
        ui_.ask_open = [this] { ask_open_file(); };
    }

    ~ShellComponent() override { ui_.ask_open = nullptr; }

    int active() const { return active_; }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        const auto [w, h] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
        const auto bp = classify(w, h);
        if (ui_.monitor) ui_.monitor->frame_rendered();
        if (bp == Breakpoint::tiny)
            return vbox({filler(), text(fmt::format(fmt::runtime(tr("tui.small.warning")), fmt::arg("w", w), fmt::arg("h", h))) | hcenter,
                         filler()});
        if (screens_.empty()) return text("");
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        screen.set_breakpoint(bp);
        Elements tabs;
        const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
        const bool compact = bp == Breakpoint::small;
        tabs.push_back(text(std::string{ascii ? " *" : " ◆"} + (compact ? " " : " Signals ")) | bold | fg(ui_.theme.accent, ui_));
        tabs.push_back(text(std::string{glyph(Glyph::v, ui_.caps.glyphs)}) | fg(ui_.theme.dim, ui_));
        for (std::size_t i = 0; i < screens_.size(); ++i) {
            const bool on = static_cast<int>(i) == active_;
            auto label = text(" " + std::to_string(i + 1) + (compact && !on ? "" : " " + screens_[i]->tab_label()) + " ");
            label = static_cast<int>(i) == active_ ? label | bold | fg(ui_.theme.bg, ui_) | bg(ui_.theme.accent, ui_)
                                                    : label | fg(ui_.theme.fg, ui_);
            tabs.push_back(label | reflect(tab_boxes_[i]));
        }
        tabs.push_back(filler());
        tabs.push_back(text(screen.context() + "  ") | fg(ui_.theme.dim, ui_));
        tabs.push_back(key_label(" ? ", ui_));
        Elements hints;
        auto all = screen.hints();
        const auto globals = global_hints();
        auto hint_width = [](const Hint& hint) {
            return static_cast<int>(split_codepoints(hint.key).size() + split_codepoints(hint.label).size()) + 5;
        };
        int room = w;
        for (const auto& g : globals) room -= hint_width(g);
        std::vector<Hint> row;
        for (const auto& hint : all) {
            if (hint_width(hint) > room) break;
            room -= hint_width(hint);
            row.push_back(hint);
        }
        for (const auto& g : globals) {
            all.push_back(g);
            row.push_back(g);
        }
        for (const auto& hint : row) {
            hints.push_back(key_label(" " + hint.key + " ", ui_));
            hints.push_back(text(" " + hint.label + "  ") | fg(ui_.theme.dim, ui_));
        }
        auto resources = ui_.resources ? ui_.resources() : ResourceSnapshot{};
        auto base = vbox({hbox(std::move(tabs)), screen.Render() | flex, hbox(std::move(hints)),
                          status_line(ui_, resources) | reflect(status_box_)});
        if (palette_.is_open()) return dbox({base, palette_.render(ui_) | clear_under | center});
        if (check_.is_open()) return dbox({base, check_.render(ui_) | clear_under | center});
        if (resources_open_) return dbox({base, resources_panel(ui_) | clear_under | center});
        if (!help_open_) return base;
        return dbox({base, help_overlay(all, help_filter_, ui_) | clear_under | center});
    }

    bool OnEvent(ftxui::Event e) override {
        if (palette_.is_open()) return palette_.on_event(e);
        if (check_.is_open()) return check_.on_event(e, ui_);
        if (resources_open_) {
            if (e == ftxui::Event::Escape || e == ftxui::Event::Character('R')) resources_open_ = false;
            return true;
        }
        if (help_open_) return help_event(e);
        if (e == ftxui::Event::CtrlP || e == ftxui::Event::Character(':')) {
            palette_.open(commands());
            return true;
        }
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        if (screen.capturing_text()) return screen.OnEvent(e);
        if (e.is_mouse()) {
            const auto& m = e.mouse();
            if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed && status_box_.Contain(m.x, m.y)) {
                resources_open_ = true;
                return true;
            }
            if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed)
                for (std::size_t i = 0; i < tab_boxes_.size(); ++i)
                    if (tab_boxes_[i].Contain(m.x, m.y)) {
                        active_ = static_cast<int>(i);
                        return true;
                    }
            return screen.OnEvent(e);
        }
        if (e.is_character()) {
            const auto c = e.character();
            if (c.size() == 1 && c[0] >= '1' && c[0] <= '9') {
                const int idx = c[0] - '1';
                if (idx < static_cast<int>(screens_.size())) active_ = idx;
                return true;
            }
            if (c == "?") {
                help_open_ = true;
                help_filter_.clear();
                return true;
            }
            if (c == "q") {
                if (on_quit_) on_quit_();
                return true;
            }
            if (c == "R") {
                resources_open_ = true;
                return true;
            }
        }
        return screen.OnEvent(e);
    }

private:
    void ask_open_file() {
        palette_.ask_path(tr("tui.cmd.open_prompt"), [this](std::string path) {
            if (ui_.open_file) ui_.open_file(path);
        });
    }

    std::vector<Command> commands() {
        std::vector<Command> out;
        for (std::size_t i = 0; i < screens_.size(); ++i)
            out.push_back({"tab." + std::to_string(i + 1), tr("tui.cmd.tab") + std::to_string(i + 1) + " " + screens_[i]->tab_label(),
                           [this, i] { active_ = static_cast<int>(i); }});
        out.push_back({"file.open", tr("tui.cmd.open"), [this] { ask_open_file(); }});
        out.push_back({"theme.dark", tr("tui.cmd.theme_dark"), [this] { ui_.theme = builtin_theme("dark"); }});
        out.push_back({"theme.light", tr("tui.cmd.theme_light"), [this] { ui_.theme = builtin_theme("light"); }});
        out.push_back({"glyphs.unicode", tr("tui.cmd.glyphs_unicode"), [this] { ui_.caps.glyphs = GlyphMode::unicode; }});
        out.push_back({"glyphs.ascii", tr("tui.cmd.glyphs_ascii"), [this] { ui_.caps.glyphs = GlyphMode::ascii; }});
        out.push_back({"color.true", tr("tui.cmd.color_true"), [this] { ui_.caps.color = ColorDepth::truecolor; }});
        out.push_back({"color.256", tr("tui.cmd.color_256"), [this] { ui_.caps.color = ColorDepth::palette256; }});
        out.push_back({"color.16", tr("tui.cmd.color_16"), [this] { ui_.caps.color = ColorDepth::ansi16; }});
        out.push_back({"color.none", tr("tui.cmd.color_none"), [this] { ui_.caps.color = ColorDepth::none; }});
        out.push_back({"terminal.check", tr("tui.cmd.check"), [this] { check_.open(); }});
        out.push_back({"resources", tr("tui.cmd.resources"), [this] { resources_open_ = true; }});
        out.push_back({"help", tr("tui.cmd.help"), [this] { help_open_ = true; help_filter_.clear(); }});
        out.push_back({"quit", tr("tui.cmd.quit"), [this] { if (on_quit_) on_quit_(); }});
        return out;
    }

    bool help_event(const ftxui::Event& e) {
        if (e == ftxui::Event::Escape || e == ftxui::Event::Character('?')) {
            help_open_ = false;
            return true;
        }
        if (e == ftxui::Event::Backspace) {
            pop_codepoint(help_filter_);
            return true;
        }
        if (e.is_character()) help_filter_ += e.character();
        return true;
    }

    UiContext& ui_;
    std::vector<std::shared_ptr<TabScreen>> screens_;
    std::function<void()> on_quit_;
    std::vector<ftxui::Box> tab_boxes_;
    ftxui::Box status_box_;
    int active_ = 0;
    bool help_open_ = false;
    bool resources_open_ = false;
    std::string help_filter_;
    Palette palette_;
    TerminalCheck check_;
};

}  // namespace

ftxui::Component make_shell(UiContext& ui, std::vector<std::shared_ptr<TabScreen>> screens, std::function<void()> on_quit) {
    return std::make_shared<ShellComponent>(ui, std::move(screens), std::move(on_quit));
}

int active_tab(const ftxui::Component& shell) {
    auto s = std::dynamic_pointer_cast<ShellComponent>(shell);
    return s ? s->active() : -1;
}

}  // namespace signals::tui
