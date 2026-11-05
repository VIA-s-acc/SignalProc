#pragma once

#include <ostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/check.hpp"

namespace signals::cli {

using CheckSuite = std::vector<core::CheckResult> (*)();

std::vector<CheckSuite>& check_suites();

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err);

}  // namespace signals::cli
