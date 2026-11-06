#pragma once

#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

class TerminalCheck {
public:
    void open() { open_ = true; status_.clear(); }
    void close() { open_ = false; }
    bool is_open() const { return open_; }
    bool on_event(const ftxui::Event& e, UiContext& ui);
    ftxui::Element render(const UiContext& ui) const;

private:
    bool open_ = false;
    std::string status_;
};

}  // namespace signals::tui
