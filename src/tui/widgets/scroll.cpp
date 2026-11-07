#include "tui/widgets/scroll.hpp"

#include <algorithm>
#include <limits>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/list_window.hpp"

namespace signals::tui {

ftxui::Element scrolled(std::vector<ftxui::Element> rows, Scroll& scroll, int height, std::optional<std::size_t> follow, const UiContext& ui, int row_height) {
    using namespace ftxui;
    if (rows.empty()) return text("");
    const auto window = list_window(scroll.first, rows.size(), std::max(1, height), std::max(1, row_height), follow);
    const bool ascii = ui.caps.glyphs == GlyphMode::ascii;
    auto marker = [&](bool up, std::size_t count) {
        const std::string arrow = ascii ? (up ? "^ " : "v ") : (up ? "▲ " : "▼ ");
        return text(arrow + fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count))) | fg(ui.theme.dim, ui);
    };
    Elements out;
    if (window.first > 0) out.push_back(marker(true, window.first));
    for (std::size_t k = window.first; k < window.first + window.shown; ++k) out.push_back(std::move(rows[k]));
    if (window.first + window.shown < rows.size()) out.push_back(marker(false, rows.size() - window.first - window.shown));
    return vbox(std::move(out));
}

bool wheel_scroll(const ftxui::Mouse& m, const ftxui::Box& box, Scroll& scroll, int step) {
    if (m.button != ftxui::Mouse::WheelDown && m.button != ftxui::Mouse::WheelUp) return false;
    if (box.x_max < box.x_min || !box.Contain(m.x, m.y)) return false;
    if (m.button == ftxui::Mouse::WheelDown) scroll.first += static_cast<std::size_t>(step);
    else scroll.first = scroll.first > static_cast<std::size_t>(step) ? scroll.first - static_cast<std::size_t>(step) : 0;
    return true;
}

bool key_scroll(const ftxui::Event& e, Scroll& scroll, bool arrows, std::size_t page) {
    using ftxui::Event;
    auto& first = scroll.first;
    if (arrows && e == Event::ArrowUp) return first = first > 0 ? first - 1 : 0, true;
    if (arrows && e == Event::ArrowDown) return ++first, true;
    if (e == Event::PageUp) return first = first > page ? first - page : 0, true;
    if (e == Event::PageDown) return first += page, true;
    if (e == Event::Home) return first = 0, true;
    if (e == Event::End) return first = std::numeric_limits<std::size_t>::max() / 2, true;
    return false;
}

}  // namespace signals::tui
