#include "tui/docs.hpp"

#include <set>

#include "i18n/i18n.hpp"
#include "tui/app_screens.hpp"
#include "tui/shell/shell.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {
namespace {

std::string cell(std::string s) {
    std::string out;
    for (const char c : s) out += c == '|' ? std::string{"\\\\|"} : std::string{c};
    return out;
}

std::string table(const std::vector<Hint>& hints) {
    std::string out = "| " + tr("docs.keys.col.key") + " | " + tr("docs.keys.col.action") + " |\n|---|---|\n";
    for (const auto& h : hints) out += "| `" + cell(h.key) + "` | " + cell(h.label) + " |\n";
    return out + "\n";
}

}  // namespace

std::string keys_markdown() {
    UiContext ui;
    ui.theme = builtin_theme("dark");
    ui.caps = {ColorDepth::truecolor, GlyphMode::unicode, "docs"};
    ui.terminal_size = [] { return std::pair{150, 44}; };
    auto models = make_app_models();
    auto screens = make_app_screens(ui, models);
    std::string out = "# " + tr("docs.keys.title") + "\n\n" + tr("docs.keys.generated") + "\n\n";
    out += "## " + tr("tui.help.everywhere") + "\n\n" + table(global_help());
    for (std::size_t t = 0; t < screens.size(); ++t) {
        auto& s = *screens[t];
        const auto heading = std::to_string(t + 1) + " " + s.tab_label();
        if (!s.active_child()) {
            out += "## " + heading + "\n\n" + table(s.hints());
            continue;
        }
        std::set<std::string> seen;
        for (std::size_t c = 0; c < 8; ++c) {
            s.select_child(c);
            const auto child = s.active_child();
            if (!child || !seen.insert(child->tab_label()).second) break;
            out += "## " + heading + " ▸ " + child->tab_label() + "\n\n" + table(s.hints());
        }
        s.select_child(0);
    }
    return out;
}

}  // namespace signals::tui
