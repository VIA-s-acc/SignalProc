#pragma once

#include <map>
#include <set>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

struct DiagramSpec {
    std::set<std::string> highlight;
    std::string down = "↓2";
    std::string up = "↑2";
};

ftxui::Element bank_diagram(const DiagramSpec& spec, const UiContext& ui, std::map<std::string, ftxui::Box>* boxes = nullptr);

}  // namespace signals::tui
