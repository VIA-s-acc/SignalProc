#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/expr.hpp"
#include "learn/task.hpp"

namespace signals::learn {

core::Result<std::vector<double>> parse_answer(std::string_view text, const Answer& a);

struct AnswerCheck {
    bool passed = false;
    std::string detail;
};

AnswerCheck compare_answer(std::span<const double> given, std::span<const double> expected, const Answer& a);
std::string format_answer(std::span<const double> values, const Answer& a);
std::string describe(const core::ExprError& e);

}  // namespace signals::learn
