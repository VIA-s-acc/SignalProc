#pragma once

#include <cstddef>
#include <optional>

namespace signals::tui {

struct ListWindow {
    std::size_t first = 0, shown = 0;
};

ListWindow list_window(std::size_t& first, std::size_t n, int budget, int row_height, std::optional<std::size_t> follow);

}  // namespace signals::tui
