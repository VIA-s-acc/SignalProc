#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/check.hpp"

namespace signals::tui {

std::string subscript_number(std::size_t n);
std::string yes_no(bool v);
std::string joined(const std::vector<std::string>& parts, std::string_view sep = " · ");
core::CheckResult renamed(core::CheckResult r, std::string id);

}  // namespace signals::tui
