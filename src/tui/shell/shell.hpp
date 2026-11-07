#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

ftxui::Component make_shell(UiContext& ui, std::vector<std::shared_ptr<TabScreen>> screens, std::function<void()> on_quit);
int active_tab(const ftxui::Component& shell);

std::vector<std::shared_ptr<TabScreen>> placeholder_screens(UiContext& ui);
std::shared_ptr<TabScreen> make_placeholder(std::string label, std::string title, std::string description, std::string stage,
                                            UiContext* ui = nullptr);

ftxui::Element status_line(const UiContext& ui, const ResourceSnapshot& r, int width);
std::vector<Hint> global_help();
ftxui::Element help_overlay(const std::vector<Hint>& screen, const std::string& filter, const UiContext& ui, int height, std::size_t& scroll);
std::vector<Hint> global_hints();

}  // namespace signals::tui
