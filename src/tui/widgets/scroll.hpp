#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

namespace signals::tui {

struct UiContext;

struct Scroll {
    std::size_t first = 0;
};

ftxui::Element scrolled(std::vector<ftxui::Element> rows, Scroll& scroll, int height, std::optional<std::size_t> follow, const UiContext& ui, int row_height = 1);
bool wheel_scroll(const ftxui::Mouse& m, const ftxui::Box& box, Scroll& scroll, int step = 3);
bool key_scroll(const ftxui::Event& e, Scroll& scroll, bool arrows = true, std::size_t page = 5);

}  // namespace signals::tui
