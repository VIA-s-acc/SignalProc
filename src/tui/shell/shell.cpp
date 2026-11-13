#include "core/version.hpp"
#include "tui/shell/shell.hpp"

#include "tui/screens/theory_navigator.hpp"

#include <cstdlib>

#include <fmt/format.h>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "i18n/i18n.hpp"
#include "tui/monitor/resources_panel.hpp"
#include "tui/shell/overlays.hpp"
#include "tui/shell/palette.hpp"
#include "tui/shell/plot_export.hpp"
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
        ui_.navigate = [this](std::string_view target, const NavArgs& args) { navigate(target, args); };
        ui_.open_palette = [this](std::string filter) { palette_.open(commands(), std::move(filter)); };
        ui_.choose = [this](std::string title, std::vector<Command> items) { palette_.open(std::move(items), {}, std::move(title)); };
        ui_.ask_text = [this](std::string prompt, std::string initial, std::function<void(std::string)> done) {
            palette_.ask_text(std::move(prompt), std::move(initial), std::move(done));
        };
        ui_.notice = [this](std::string text) { notice_ = std::move(text); };
    }

    ~ShellComponent() override {
        ui_.ask_open = nullptr;
        ui_.navigate = nullptr;
        ui_.open_palette = nullptr;
        ui_.choose = nullptr;
        ui_.ask_text = nullptr;
        ui_.notice = nullptr;
    }

    int active() const { return active_; }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        const auto [w, h] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
        const auto bp = classify(w, h);
        if (ui_.monitor) ui_.monitor->frame_rendered();
        if (ui_.ticker) ui_.ticker->begin_frame();
        if (ui_.player) {
            ui_.player->poll();
            if (auto why = ui_.player->take_interruption(); why && ui_.warn)
                ui_.warn(fmt::format(fmt::runtime(tr("tui.audio.interrupted")), fmt::arg("reason", *why)));
            if (ui_.ticker) ui_.ticker->want("player", ui_.player->state() == audio::PlayState::playing);
        }
        auto paint = [&](Element e) { return ui_.caps.color == ColorDepth::none ? e : e | bg(ui_.theme.bg, ui_) | fg(ui_.theme.fg, ui_); };
        if (bp == Breakpoint::tiny)
            return paint(vbox({filler(), paragraphAlignCenter(fmt::format(fmt::runtime(tr("tui.small.warning")), fmt::arg("w", w), fmt::arg("h", h))),
                               filler()}));
        if (screens_.empty()) return paint(text(""));
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        screen.set_breakpoint(bp);
        Elements tabs;
        const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
        const bool compact = bp == Breakpoint::small;
        const std::string brand = std::string{ascii ? " *" : " ◆"} + (compact ? " " : " Signals " + std::string{core::version()} + " ");
        int used = display_width(brand) + 1 + 2 + 3;
        tabs.push_back(text(brand) | bold | fg(ui_.theme.accent, ui_));
        tabs.push_back(text(std::string{glyph(Glyph::v, ui_.caps.glyphs)}) | fg(ui_.theme.dim, ui_));
        for (std::size_t i = 0; i < screens_.size(); ++i) {
            const bool on = static_cast<int>(i) == active_;
            const auto caption = " " + std::to_string(i + 1) + (compact && !on ? "" : " " + screens_[i]->tab_label()) + " ";
            used += display_width(caption);
            auto label = text(caption);
            label = static_cast<int>(i) == active_ ? label | bold | fg(ui_.theme.bg, ui_) | bg(ui_.theme.accent, ui_)
                                                    : label | fg(ui_.theme.fg, ui_);
            tabs.push_back(label | reflect(tab_boxes_[i]));
        }
        tabs.push_back(filler());
        tabs.push_back(screen.context_element(ui_, std::max(0, w - used)));
        tabs.push_back(text("  "));
        tabs.push_back(key_label(" ? ", ui_) | reflect(help_box_));
        Elements hints;
        if (!notice_.empty()) {
            hints.push_back(text(ellipsize(std::string{ascii ? "> " : "✓ "} + notice_, w, ascii)) | fg(ui_.theme.ok, ui_));
        } else if (const auto about = hover_ ? screen.explain_at(hover_->first, hover_->second) : std::nullopt) {
            hints.push_back(text(ellipsize(std::string{ascii ? "i " : "ⓘ "} + *about, w, ascii)) | fg(ui_.theme.accent, ui_));
        }
        auto all = screen.hints();
        const auto globals = global_hints();
        auto hint_width = [](const Hint& hint) {
            return static_cast<int>(split_codepoints(hint.key).size() + split_codepoints(hint.label).size()) + 5;
        };
        std::vector<Hint> visible;
        for (const auto& g : globals)
            if (w >= 120 || g.key == "?" || g.key == ":") visible.push_back(g);
        int room = w;
        for (const auto& g : visible) room -= hint_width(g);
        const auto mouse = [](const Hint& hint) {
            for (const char* key : {"tui.signal.key.wheel", "tui.signal.key.drag", "tui.signal.key.click", "tui.key.right_click"})
                if (hint.key.find(tr(key)) != std::string::npos) return true;
            return false;
        };
        std::vector<Hint> row;
        auto candidates = all;
        candidates.push_back({"'", tr("tui.hint.jump")});
        for (const auto& hint : candidates) {
            if (mouse(hint) || hint_width(hint) > room) continue;
            room -= hint_width(hint);
            row.push_back(hint);
        }
        for (const auto& g : visible) row.push_back(g);
        if (hints.empty())
            for (const auto& hint : row) {
                hints.push_back(key_label(" " + hint.key + " ", ui_));
                hints.push_back(text(" " + hint.label + "  ") | fg(ui_.theme.dim, ui_));
            }
        auto resources = ui_.resources ? ui_.resources() : ResourceSnapshot{};
        auto base = vbox({hbox(std::move(tabs)), screen.Render() | flex, hbox(std::move(hints)),
                          status_line(ui_, resources, w) | reflect(status_box_)});
        if (palette_.is_open()) return paint(dbox({base, palette_.render(ui_, w) | clear_under | center}));
        if (check_.is_open()) return paint(dbox({base, check_.render(ui_) | clear_under | center}));
        if (resources_open_) return paint(dbox({base, resources_panel(ui_, w, h, resources_scroll_) | clear_under | center}));
        if (menu_.is_open()) return paint(dbox({base, menu_.render(ui_, w, h)}));
        if (jump_.is_active()) return paint(dbox({base, jump_.render(ui_)}));
        if (!help_open_) return paint(base);
        return paint(dbox({base, help_overlay(all, help_filter_, ui_, h, help_scroll_) | clear_under | center}));
    }

    bool OnEvent(ftxui::Event e) override {
        if (!e.is_mouse() && e != ftxui::Event::Custom) {
            hover_.reset();
            notice_.clear();
        }
        if (palette_.is_open()) return palette_.on_event(e);
        if (check_.is_open()) return check_.on_event(e, ui_);
        if (resources_open_) {
            const auto step = [this](float d) { resources_scroll_ = std::clamp(resources_scroll_ + d, 0.f, 1.f); };
            if (e == ftxui::Event::Escape || e == ftxui::Event::Character('R')) resources_open_ = false;
            else if (e == ftxui::Event::ArrowDown) step(0.1f);
            else if (e == ftxui::Event::ArrowUp) step(-0.1f);
            else if (e == ftxui::Event::PageDown) step(0.5f);
            else if (e == ftxui::Event::PageUp) step(-0.5f);
            else if (e == ftxui::Event::Home) resources_scroll_ = 0;
            else if (e == ftxui::Event::End) resources_scroll_ = 1;
            else if (e.is_mouse() && e.mouse().button == ftxui::Mouse::WheelDown) step(0.1f);
            else if (e.is_mouse() && e.mouse().button == ftxui::Mouse::WheelUp) step(-0.1f);
            return true;
        }
        if (help_open_) return help_event(e);
        if (menu_.is_open()) return menu_.on_event(e);
        if (jump_.is_active()) return jump_event(e);
        if (e == ftxui::Event::CtrlP || e == ftxui::Event::Character(':')) {
            palette_.open(commands());
            return true;
        }
        if (e == ftxui::Event::F1) {
            open_theory_for_focus();
            return true;
        }
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        if (screen.capturing_text()) return screen.OnEvent(e);
        if (e.is_mouse()) {
            const auto& m = e.mouse();
            hover_ = std::pair{m.x, m.y};
            if (m.button == ftxui::Mouse::Right && m.motion == ftxui::Mouse::Pressed) {
                auto items = screen.context_menu(m.x, m.y);
                if (!items.empty()) {
                    menu_.open(std::move(items), m.x, m.y);
                    return true;
                }
            }
            if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed && help_box_.Contain(m.x, m.y)) {
                help_open_ = true;
                help_filter_.clear();
                help_scroll_ = 0;
                return true;
            }
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
            if (c == "'") {
                start_jump();
                return true;
            }
            if (c == "?") {
                help_open_ = true;
                help_filter_.clear();
                help_scroll_ = 0;
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
            if (c == "m") {
                const auto anchor = screen.menu_anchor();
                const auto [w, h] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
                const int x = anchor ? anchor->first : w / 2, y = anchor ? anchor->second : h / 2;
                auto items = screen.context_menu(x, y);
                if (items.empty()) return screen.OnEvent(e);
                menu_.open(std::move(items), x, y);
                return true;
            }
        }
        return screen.OnEvent(e);
    }

private:
    std::string notice_;
    void start_jump() {
        std::vector<JumpTarget> targets;
        for (std::size_t i = 0; i < tab_boxes_.size(); ++i) targets.push_back({tab_boxes_[i], screens_[i]->tab_label()});
        for (auto& t : screens_[static_cast<std::size_t>(active_)]->jump_targets()) targets.push_back(std::move(t));
        targets.push_back({status_box_, tr("tui.cmd.resources")});
        jump_.start(std::move(targets));
    }

    bool jump_event(ftxui::Event e) {
        if (e == ftxui::Event::Custom) return false;
        if (e.is_mouse() && (e.mouse().motion == ftxui::Mouse::Moved || e.mouse().button == ftxui::Mouse::None)) return true;
        if (!e.is_character()) {
            jump_.cancel();
            return true;
        }
        const auto box = jump_.on_key(e.character());
        if (!box) return true;
        ftxui::Mouse m;
        m.button = ftxui::Mouse::Left;
        m.x = (box->x_min + box->x_max) / 2;
        m.y = (box->y_min + box->y_max) / 2;
        m.motion = ftxui::Mouse::Pressed;
        OnEvent(ftxui::Event::Mouse("", m));
        m.motion = ftxui::Mouse::Released;
        OnEvent(ftxui::Event::Mouse("", m));
        return true;
    }

    void choose_recent() {
        std::vector<Command> items;
        const auto add = [&](RecentKind kind, const char* label) {
            for (const auto& file : ui_.session->recent(kind)) {
                const auto u8 = file.filename().u8string(), parent = file.parent_path().u8string();
                std::string name{reinterpret_cast<const char*>(u8.data()), u8.size()}, where{reinterpret_cast<const char*>(parent.data()), parent.size()};
                items.push_back({std::string{"recent."} + label + "." + std::to_string(items.size()), name,
                                 [this, kind, file] {
                                     if (kind == RecentKind::audio && ui_.open_file) {
                                         ui_.open_file(file);
                                         active_ = 0;
                                     }
                                     if (kind == RecentKind::graph && ui_.open_graph) ui_.open_graph(file);
                                 },
                                 fmt::format(fmt::runtime(tr(std::string{"tui.recent."} + label)), fmt::arg("dir", where))});
            }
        };
        add(RecentKind::audio, "audio");
        add(RecentKind::graph, "graph");
        if (items.empty()) {
            notice_ = tr("tui.recent.none");
            return;
        }
        if (ui_.choose) ui_.choose(tr("tui.recent.title"), std::move(items));
    }

    void ask_open_file() {
        palette_.ask_path(tr("tui.cmd.open_prompt"), [this](std::string path) {
            if (ui_.open_file) ui_.open_file(expand_path(path));
        });
    }

    void navigate(std::string_view target, const NavArgs& args) {
        struct Route {
            std::string_view target;
            int tab;
            int sub;
        };
        static constexpr Route routes[]{{"signal", 0, -1},     {"spectrum", 1, 0}, {"resample", 1, 1},       {"tempo", 1, 2},       {"filters", 2, -1},
                                        {"designer", 3, 0},    {"diagram", 3, 1},  {"decomposition", 4, -1}, {"processing", 6, -1},   {"pipeline", 7, -1},
                                        {"matrix", 5, -1},     {"explain", 8, 0},  {"under_the_hood", 8, 1}, {"tutorial", 8, 2}, {"learn", 8, 3}};
        int tab = -1, sub = -1;
        if (target.starts_with("tab:")) {
            const auto n = std::atoi(std::string{target.substr(4)}.c_str());
            tab = n - 1;
        } else {
            for (const auto& r : routes)
                if (r.target == target) tab = r.tab, sub = r.sub;
        }
        if (tab < 0 || tab >= static_cast<int>(screens_.size())) return;
        active_ = tab;
        auto& screen = *screens_[static_cast<std::size_t>(tab)];
        if (sub >= 0) screen.select_child(static_cast<std::size_t>(sub));
        screen.open_with(args);
    }

    void export_plots() {
        if (screens_.empty()) return;
        auto& screen = *screens_[static_cast<std::size_t>(active_)];
        auto plots = screen.plot_exports();
        auto maps = screen.heatmap_exports();
        auto charts = screen.chart_exports();
        std::vector<Command> items;
        for (auto& p : plots) items.push_back({"plot." + p.id, p.title, [this, p] { export_plot(ui_, p); }});
        for (auto& m : maps) items.push_back({"map." + m.id, m.title, [this, m] { export_heatmap(ui_, m); }});
        for (auto& c : charts) items.push_back({"chart." + c.id, c.title, [this, c] { export_chart(ui_, c); }});
        if (items.empty()) {
            notice_ = tr("tui.export.none");
            return;
        }
        if (items.size() == 1) return items.front().run();
        if (ui_.choose) ui_.choose(tr("tui.export.choose"), std::move(items));
    }

    void open_theory_for_focus() {
        if (!ui_.open_theory || screens_.empty()) return;
        std::shared_ptr<TabScreen> target = screens_[static_cast<std::size_t>(active_)];
        while (auto child = target->active_child()) target = child;
        ui_.open_theory(make_theory_page(target, "tab:" + std::to_string(active_ + 1)));
    }

    std::vector<Command> commands() {
        std::vector<Command> out = screens_.empty() ? std::vector<Command>{} : screens_[static_cast<std::size_t>(active_)]->commands();
        for (std::size_t i = 0; i < screens_.size(); ++i)
            out.push_back({"tab." + std::to_string(i + 1), tr("tui.cmd.tab") + std::to_string(i + 1) + " " + screens_[i]->tab_label(),
                           [this, i] { active_ = static_cast<int>(i); }});
        out.push_back({"file.open", tr("tui.cmd.open"), [this] { ask_open_file(); }});
        if (ui_.session) {
            out.push_back({"file.recent", tr("tui.cmd.recent"), [this] { choose_recent(); }});
            out.push_back({"file.forget_recent", tr("tui.cmd.forget_recent"), [this] {
                               ui_.session->forget_recent();
                               notice_ = tr("tui.recent.forgotten");
                           }});
        }
        for (const char* name : {"dark", "light"})
            out.push_back({std::string{"theme."} + name, tr(std::string{"tui.cmd.theme_"} + name), [this, name] {
                               ui_.theme = builtin_theme(name);
                               if (ui_.session) ui_.session->set_theme(name);
                           }});
        out.push_back({"glyphs.unicode", tr("tui.cmd.glyphs_unicode"), [this] { ui_.caps.glyphs = GlyphMode::unicode; }});
        out.push_back({"glyphs.ascii", tr("tui.cmd.glyphs_ascii"), [this] { ui_.caps.glyphs = GlyphMode::ascii; }});
        out.push_back({"color.true", tr("tui.cmd.color_true"), [this] { ui_.caps.color = ColorDepth::truecolor; }});
        out.push_back({"color.256", tr("tui.cmd.color_256"), [this] { ui_.caps.color = ColorDepth::palette256; }});
        out.push_back({"color.16", tr("tui.cmd.color_16"), [this] { ui_.caps.color = ColorDepth::ansi16; }});
        out.push_back({"color.none", tr("tui.cmd.color_none"), [this] { ui_.caps.color = ColorDepth::none; }});
        out.push_back({"terminal.check", tr("tui.cmd.check"), [this] { check_.open(); }});
        out.push_back({"plot.export", tr("tui.cmd.export_plot"), [this] { export_plots(); }});
        out.push_back({"settings.help", tr("tui.cmd.settings_help"), [this] {
                           if (ui_.open_theory) ui_.open_theory(TheoryPage{TheoryLink{TheoryLink::Kind::section, "settings", tr("tui.cmd.settings_help")}});
                       }});
        out.push_back({"editor", tr("tui.cmd.editor"), [this] {
                           if (!ui_.ask_text) return;
                           ui_.ask_text(tr("tui.cmd.editor_prompt"), ui_.editor, [this](std::string value) {
                               const auto first = value.find_first_not_of(' ');
                               value = first == std::string::npos ? std::string{} : value.substr(first, value.find_last_not_of(' ') - first + 1);
                               ui_.editor = value;
                               if (ui_.prefs_file.empty()) return;
                               auto saved = load_prefs(ui_.prefs_file).value_or(TerminalPrefs{});
                               saved.editor = value.empty() ? std::nullopt : std::optional{value};
                               (void)save_prefs(ui_.prefs_file, saved);
                           });
                       }});
        out.push_back({"language.en", tr("tui.cmd.language_en"), [this] {
                           if (ui_.switch_language) ui_.switch_language(Language::en);
                       }});
        out.push_back({"language.ru", tr("tui.cmd.language_ru"), [this] {
                           if (ui_.switch_language) ui_.switch_language(Language::ru);
                       }});
        out.push_back({"resources", tr("tui.cmd.resources"), [this] { resources_open_ = true; }});
        out.push_back({"jump", tr("tui.cmd.jump"), [this] { start_jump(); }});
        out.push_back({"help", tr("tui.cmd.help"), [this] { help_open_ = true; help_filter_.clear(); }});
        out.push_back({"quit", tr("tui.cmd.quit"), [this] { if (on_quit_) on_quit_(); }});
        out.push_back({"theory.focus", tr("tui.cmd.theory_focus"), [this] { open_theory_for_focus(); }});
        return out;
    }

    bool help_event(ftxui::Event e) {
        if (e == ftxui::Event::Escape || e == ftxui::Event::Character('?')) {
            help_open_ = false;
            return true;
        }
        if (e == ftxui::Event::Backspace) {
            pop_codepoint(help_filter_);
            help_scroll_ = 0;
            return true;
        }
        const auto [w, h] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
        const auto page = static_cast<std::size_t>(std::max(1, h - 8));
        if (e == ftxui::Event::ArrowDown) return ++help_scroll_, true;
        if (e == ftxui::Event::ArrowUp) return help_scroll_ -= help_scroll_ > 0 ? 1 : 0, true;
        if (e == ftxui::Event::PageDown) return help_scroll_ += page, true;
        if (e == ftxui::Event::PageUp) return help_scroll_ -= std::min(help_scroll_, page), true;
        if (e == ftxui::Event::Home) return help_scroll_ = 0, true;
        if (e == ftxui::Event::End) return help_scroll_ = static_cast<std::size_t>(-1) / 2, true;
        if (e.is_mouse()) {
            const auto& m = e.mouse();
            if (m.button == ftxui::Mouse::WheelDown) ++help_scroll_;
            if (m.button == ftxui::Mouse::WheelUp && help_scroll_ > 0) --help_scroll_;
            return true;
        }
        if (e.is_character()) {
            help_filter_ += e.character();
            help_scroll_ = 0;
        }
        return true;
    }

    UiContext& ui_;
    std::vector<std::shared_ptr<TabScreen>> screens_;
    std::function<void()> on_quit_;
    std::vector<ftxui::Box> tab_boxes_;
    ftxui::Box status_box_, help_box_{1, 0, 1, 0};
    int active_ = 0;
    bool help_open_ = false;
    bool resources_open_ = false;
    std::optional<std::pair<int, int>> hover_;
    std::string help_filter_;
    std::size_t help_scroll_ = 0;
    float resources_scroll_ = 0;
    Palette palette_;
    TerminalCheck check_;
    ContextMenu menu_;
    JumpMode jump_;
};

}  // namespace

ftxui::Component make_shell(UiContext& ui, std::vector<std::shared_ptr<TabScreen>> screens, std::function<void()> on_quit) {
    return std::make_shared<ShellComponent>(ui, std::move(screens), std::move(on_quit));
}

ftxui::Element TabScreen::context_element(const UiContext& ui, int width) {
    return ftxui::text(ellipsize(context(), width, ui.caps.glyphs == GlyphMode::ascii)) | fg(ui.theme.dim, ui);
}

int active_tab(const ftxui::Component& shell) {
    auto s = std::dynamic_pointer_cast<ShellComponent>(shell);
    return s ? s->active() : -1;
}

}  // namespace signals::tui
