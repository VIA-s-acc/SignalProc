#pragma once

#include <span>
#include <string>
#include <vector>

#include "learn/task.hpp"

namespace signals::learn {

struct LintIssue {
    std::string task, message;
};

std::vector<LintIssue> lint_task(const Task& t, std::span<const int> variants);
std::vector<LintIssue> lint_course(const Course& c, std::span<const int> variants);

}  // namespace signals::learn
