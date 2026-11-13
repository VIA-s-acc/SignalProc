#pragma once

#include <string>

#include "core/expr.hpp"

namespace signals::pipeline {

std::string describe_expr_error(const core::ExprError& e);

}  // namespace signals::pipeline
