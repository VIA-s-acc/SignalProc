#include "tui/shell/shell.hpp"

#include <fmt/format.h>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"

namespace signals::tui {
namespace {

class ShellComponent final : public ftxui::ComponentBase {
public:
    ShellComponent(UiContext& ui, std::vector<std::shared_ptr<TabScreen>> screens, std::function<void()> on_quit)
        : ui_{ui}, screens_{std::move(screens)}, on_quit_{std::move(on_quit)}, tab_boxes_(screens_.size()) {}

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
        for (const auto& g : global_hints()) all.push_back(g);
        for (const auto& hint : all) {
            hints.push_back(key_label(" " + hint.key + " ", ui_));
            hints.push_back(text(" " + hint.label + "  ") | fg(ui_.theme.dim, ui_));
        }
        auto resources = ui_.resources ? ui_.resources() : ResourceSnapshot{};
        auto base = vbox({hbox(std::move(tabs)), screen.Render() | flex, hbox(std::move(hints)),
                          status_line(ui_, resources) | reflect(status_box_)});
        if (!help_open_) return base;
        auto overlay_hints = all;
        return dbox({base, help_overlay(overlay_hints, help_filter_, ui_) | clear_under | center});
    }

    bool OnEvent(ftxui::Event e) override {
        if (help_open_) return help_event(e);
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        if (screen.capturing_text()) return screen.OnEvent(e);
        if (e.is_mouse()) {
            const auto& m = e.mouse();
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
        }
        return screen.OnEvent(e);
    }

private:
    bool help_event(const ftxui::Event& e) {
        if (e == ftxui::Event::Escape || e == ftxui::Event::Character('?')) {
            help_open_ = false;
            return true;
        }
        if (e == ftxui::Event::Backspace) {
            while (!help_filter_.empty()) {
                const auto c = static_cast<unsigned char>(help_filter_.back());
                help_filter_.pop_back();
                if ((c & 0xC0) != 0x80) break;
            }
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
    std::string help_filter_;
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
