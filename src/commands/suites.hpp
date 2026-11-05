#pragma once

#include <vector>

#include "core/check.hpp"

namespace signals::cli {

std::vector<core::CheckResult> core_suite();

double base_tolerance(bool generated, bool approximate);

}  // namespace signals::cli
