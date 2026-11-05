#include <algorithm>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"

namespace signals::tui {

std::vector<Hint> global_hints() {
    return {{"1–9", tr("tui.hint.tabs")}, {"?", tr("tui.hint.help")}, {"q", tr("tui.hint.quit")}};
}

ftxui::Element help_overlay(const std::vector<Hint>& hints, const std::string& filter, const UiContext& ui) {
    ftxui::Elements rows;
    for (const auto& h : hints) {
        if (!filter.empty() && h.label.find(filter) == std::string::npos && h.key.find(filter) == std::string::npos) continue;
        rows.push_back(ftxui::hbox({key_label(" " + h.key + " ", ui), ftxui::text("  " + h.label)}));
    }
    if (rows.empty()) rows.push_back(ftxui::text(tr("tui.help.nothing")) | fg(ui.theme.dim, ui));
    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::text(tr("tui.help.filter") + filter + "▏") | fg(ui.theme.dim, ui));
    rows.push_back(ftxui::text(tr("tui.help.close")) | fg(ui.theme.dim, ui));
    return panel(tr("tui.help.title"), {}, ftxui::vbox(std::move(rows)) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 40), true, ui);
}

}  // namespace signals::tui
