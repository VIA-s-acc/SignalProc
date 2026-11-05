#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace signals::core {

enum class ThresholdMode { hard, soft, garrote, greater, less };

std::optional<ThresholdMode> parse_threshold_mode(std::string_view name);

std::vector<double> threshold(std::span<const double> x, double value, ThresholdMode mode, double substitute = 0.0);

std::vector<double> threshold_firm(std::span<const double> x, double low, double high);

}  // namespace signals::core
