#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

struct Chip {
    std::string label;
    bool on = false;
};

ftxui::Element panel(std::string_view title, std::vector<Chip> chips, ftxui::Element body, bool focused, const UiContext& ui,
                     ftxui::Box* title_box = nullptr);

ftxui::Decorator fg(Rgb c, const UiContext& ui);
ftxui::Decorator bg(Rgb c, const UiContext& ui);
ftxui::Element key_label(std::string_view key, const UiContext& ui);

}  // namespace signals::tui
