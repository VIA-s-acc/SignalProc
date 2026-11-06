#include <algorithm>

#include <fmt/format.h>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {

std::vector<Hint> global_hints() {
    return {{"1–9", tr("tui.hint.tabs")}, {":", tr("tui.hint.palette")}, {"R", tr("tui.hint.resources")}, {"?", tr("tui.hint.help")}, {"q", tr("tui.hint.quit")}};
}

std::vector<Hint> global_help() {
    return {{"1–9", tr("tui.hint.tabs")},
            {":  Ctrl+P", tr("tui.hint.palette")},
            {"F1", tr("tui.hint.theory")},
            {"m", tr("tui.hint.menu_key")},
            {"'", tr("tui.hint.jump")},
            {"R", tr("tui.hint.resources")},
            {"?", tr("tui.hint.help")},
            {tr("tui.key.click_help"), tr("tui.hint.help")},
            {"q", tr("tui.hint.quit")},
            {tr("tui.key.right_click"), tr("tui.hint.menu")},
            {tr("tui.key.click_tab"), tr("tui.hint.click_tab")},
            {tr("tui.key.click_status"), tr("tui.hint.resources")}};
}

ftxui::Element help_overlay(const std::vector<Hint>& screen, const std::string& filter, const UiContext& ui, int height, std::size_t& scroll) {
    using namespace ftxui;
    const auto matches = [&](const Hint& h) { return filter.empty() || h.label.find(filter) != std::string::npos || h.key.find(filter) != std::string::npos; };
    const auto global = global_help();
    std::size_t key_width = 0;
    for (const auto* list : {&screen, &global})
        for (const auto& h : *list) key_width = std::max(key_width, split_codepoints(h.key).size());
    Elements rows;
    const auto section = [&](const std::string& title, const std::vector<Hint>& hints) {
        bool any = false;
        for (const auto& h : hints) {
            if (!matches(h)) continue;
            if (!any) rows.push_back(text(title) | bold | fg(ui.theme.dim, ui));
            any = true;
            const auto pad = std::string(key_width - split_codepoints(h.key).size(), ' ');
            rows.push_back(hbox({key_label(" " + h.key + pad + " ", ui), text("  " + h.label)}));
        }
        return any;
    };
    const bool here = section(tr("tui.help.screen"), screen);
    if (here) rows.push_back(text(""));
    const bool anywhere = section(tr("tui.help.everywhere"), global);
    if (!here && !anywhere) rows.push_back(text(tr("tui.help.nothing")) | fg(ui.theme.dim, ui));
    const auto room = static_cast<std::size_t>(std::max(3, height - 2 - 4));
    const auto n = rows.size();
    scroll = n > room ? std::min(scroll, n - room) : 0;
    Elements shown(rows.begin() + static_cast<std::ptrdiff_t>(scroll), rows.begin() + static_cast<std::ptrdiff_t>(std::min(n, scroll + room)));
    shown.push_back(text(n > room ? fmt::format(fmt::runtime(tr("tui.help.range")), fmt::arg("from", scroll + 1), fmt::arg("to", std::min(n, scroll + room)),
                                                fmt::arg("count", n))
                                  : std::string{}) |
                    fg(ui.theme.dim, ui));
    shown.push_back(text(tr("tui.help.filter") + filter + "▏") | fg(ui.theme.dim, ui));
    shown.push_back(text(tr("tui.help.close")) | fg(ui.theme.dim, ui));
    return panel(tr("tui.help.title") + " · Signals " + std::string{core::version()}, {}, vbox(std::move(shown)) | size(WIDTH, GREATER_THAN, 40), true, ui);
}

}  // namespace signals::tui
