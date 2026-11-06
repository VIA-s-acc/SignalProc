#include "tui/shell/layout.hpp"

namespace signals::tui {

Breakpoint classify(int cols, int rows) {
    if (cols >= 250 && rows >= 70) return Breakpoint::xlarge;
    if (cols >= 180 && rows >= 50) return Breakpoint::large;
    if (cols >= 150 && rows >= 44) return Breakpoint::primary;
    if (cols >= 120 && rows >= 40) return Breakpoint::medium;
    if (cols >= 80 && rows >= 24) return Breakpoint::small;
    return Breakpoint::tiny;
}

PanelLayout::PanelLayout(std::vector<Size> sizes) : defaults_{std::move(sizes)} { apply_defaults(); }

void PanelLayout::resize(int cols, int rows) {
    if (dims_ == std::pair{cols, rows}) return;
    dims_ = {cols, rows};
    breakpoint_ = classify(cols, rows);
    apply_defaults();
}

int& PanelLayout::size(std::string_view key) {
    auto it = sizes_.find(key);
    if (it == sizes_.end()) it = sizes_.emplace(std::string{key}, 0).first;
    return it->second;
}

void PanelLayout::toggle_maximize(const std::string& panel) {
    if (maximized_ == panel) maximized_.reset();
    else maximized_ = panel;
}

void PanelLayout::next_panel(std::size_t count) {
    if (count == 0) return;
    small_index_ = (small_index_ + 1) % count;
}

void PanelLayout::apply_defaults() {
    const int slot = breakpoint_ == Breakpoint::tiny ? 0 : static_cast<int>(breakpoint_) - 1;
    for (const auto& d : defaults_) sizes_[d.key] = d.defaults[static_cast<std::size_t>(slot)];
}

}  // namespace signals::tui
