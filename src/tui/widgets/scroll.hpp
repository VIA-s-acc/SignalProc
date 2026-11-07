#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

namespace signals::tui {

struct UiContext;

struct Scroll {
    std::size_t first = 0;
};

ftxui::Element scrolled(std::vector<ftxui::Element> rows, Scroll& scroll, int height, std::optional<std::size_t> follow, const UiContext& ui);
bool wheel_scroll(const ftxui::Mouse& m, const ftxui::Box& box, Scroll& scroll, int step = 3);

}  // namespace signals::tui
