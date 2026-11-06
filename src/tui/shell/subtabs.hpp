#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tui/shell/screen.hpp"

namespace signals::tui {

std::shared_ptr<TabScreen> make_subtabs(std::string label, std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children,
                                        std::size_t initial = 0);

}  // namespace signals::tui
