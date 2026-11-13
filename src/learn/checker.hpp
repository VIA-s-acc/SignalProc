#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "learn/measure.hpp"
#include "learn/task.hpp"

namespace signals::learn {

struct GoalResult {
    bool passed = false;
    std::optional<double> value = {};
    std::string detail = {};
};

struct CheckResult {
    bool passed = false;
    std::vector<GoalResult> goals;
    int inputs = 0;
    std::optional<std::string> error;
    double seconds = 0;
};

struct Submission {
    std::optional<std::string> sig = {};
    std::filesystem::path base_dir = ".";
    std::optional<std::string> answer = {};
};

inline constexpr double kCheckLimitSeconds = 30;

core::Result<CheckResult> check_task(const Task& t, int variant, const Submission& s, const core::CancelToken& cancel,
                                     double limit_seconds = kCheckLimitSeconds);
core::Result<std::vector<double>> expected_answer(const Task& t, int variant);
core::Result<std::map<std::string, MeasureValue, std::less<>>> reference_measures(const Task& t, int variant);
std::string start_graph(const Task& t, int variant);
std::string solution_graph(const Task& t, int variant);
std::string render_text(std::string_view md, const Task& t, int variant);

}  // namespace signals::learn
