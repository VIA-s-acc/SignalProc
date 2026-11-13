#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "learn/progress.hpp"
#include "learn/task.hpp"

namespace signals::learn {

using GraphSource = std::function<std::optional<std::string>(std::string_view id)>;

std::string progress_report(const Course& c, const Progress& p, std::string_view version, std::string_view date, const GraphSource& graph = {});

}  // namespace signals::learn
