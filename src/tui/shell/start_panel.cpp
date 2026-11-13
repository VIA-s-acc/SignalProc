#include "tui/shell/start_panel.hpp"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "tui/data/session.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"

namespace signals::tui {

namespace {

struct Item {
    const char* title;
    const char* about;
};

constexpr std::array<Item, 3> kItems{{{"tui.start.example", "tui.start.example.about"},
                                      {"tui.start.learn", "tui.start.learn.about"},
                                      {"tui.start.open", "tui.start.open.about"}}};

}  // namespace

void StartPanel::dismiss(UiContext& ui) {
    close();
    ui.first_run_start = false;
    if (ui.session) ui.session->save_now();
}

void StartPanel::choose(std::size_t item, UiContext& ui) {
    dismiss(ui);
    if (!ui.navigate) return;
    if (item == 0) {
        ui.navigate("pipeline", NavArgs{.example = "first_steps"});
    } else if (item == 1) {
        ui.navigate("learn", NavArgs{.task = "0.1"});
    } else {
        ui.navigate("signal", {});
        if (ui.ask_open) ui.ask_open();
    }
}

bool StartPanel::on_event(ftxui::Event e, UiContext& ui) {
    using ftxui::Event;
    if (e == Event::Escape) return dismiss(ui), true;
    if (e == Event::ArrowDown || e == Event::Tab) selected_ = (selected_ + 1) % kItems.size();
    else if (e == Event::ArrowUp || e == Event::TabReverse) selected_ = (selected_ + kItems.size() - 1) % kItems.size();
    else if (e == Event::Return) choose(selected_, ui);
    else if (e == Event::Character('1') || e == Event::Character('2') || e == Event::Character('3')) choose(static_cast<std::size_t>(e.character()[0] - '1'), ui);
    else if (e.is_mouse() && e.mouse().button == ftxui::Mouse::Left && e.mouse().motion == ftxui::Mouse::Pressed) {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].Contain(e.mouse().x, e.mouse().y)) {
                choose(i, ui);
                break;
            }
    }
    return true;
}

ftxui::Element StartPanel::render(const UiContext& ui) {
    using namespace ftxui;
    const int cols = ui.terminal_size ? ui.terminal_size().first : 150;
    const int width = std::clamp(cols - 6, 20, 76);
    Elements rows{paragraph(tr("tui.start.intro")), text("")};
    for (std::size_t i = 0; i < kItems.size(); ++i) {
        const bool on = i == selected_;
        auto title = hbox({key_label(std::to_string(i + 1), ui), text(" "), text(tr(kItems[i].title)) | bold});
        auto row = vbox({on ? title | inverted : title, hbox({text("    "), paragraph(tr(kItems[i].about)) | fg(ui.theme.dim, ui)})});
        rows.push_back(row | reflect(rows_[i]));
    }
    std::string later;
    for (const auto& hint : global_hints()) later += (later.empty() ? "" : "   ") + hint.key + " " + hint.label;
    rows.push_back(text(""));
    rows.push_back(paragraph(tr("tui.start.keys")) | fg(ui.theme.dim, ui));
    rows.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.start.later")), fmt::arg("keys", later))) | fg(ui.theme.dim, ui));
    rows.push_back(paragraph(tr("tui.start.reopen")) | fg(ui.theme.dim, ui));
    const auto title = fmt::format(fmt::runtime(tr("tui.start.title")), fmt::arg("version", core::version()));
    return panel(title, {}, vbox(std::move(rows)) | size(WIDTH, LESS_THAN, width), true, ui);
}

}  // namespace signals::tui
