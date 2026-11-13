#pragma once

#include <array>
#include <cstddef>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

class StartPanel {
public:
    void open() {
        open_ = true;
        selected_ = 0;
    }
    void close() { open_ = false; }
    bool is_open() const { return open_; }
    bool on_event(ftxui::Event e, UiContext& ui);
    ftxui::Element render(const UiContext& ui);

private:
    void choose(std::size_t item, UiContext& ui);
    void dismiss(UiContext& ui);

    bool open_ = false;
    std::size_t selected_ = 0;
    std::array<ftxui::Box, 3> rows_{};
};

}  // namespace signals::tui
