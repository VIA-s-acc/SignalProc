#include "core/process/threshold.hpp"

#include <algorithm>
#include <cmath>

namespace signals::core {
namespace {

double shrink(double x, double factor) {
    if (x == 0) return x;
    return x * std::max(factor, 0.0);
}

double apply(double x, double value, ThresholdMode mode, double substitute) {
    const double m = std::abs(x);
    switch (mode) {
    case ThresholdMode::hard: return m < value ? substitute : x;
    case ThresholdMode::greater: return x < value ? substitute : x;
    case ThresholdMode::less: return x > value ? substitute : x;
    case ThresholdMode::soft: {
        const double y = shrink(x, 1 - value / m);
        return substitute != 0 && m < value ? substitute : y;
    }
    case ThresholdMode::garrote: {
        const double y = shrink(x, 1 - value * value / (m * m));
        return substitute != 0 && m < value ? substitute : y;
    }
    }
    return x;
}

}  // namespace

std::optional<ThresholdMode> parse_threshold_mode(std::string_view name) {
    if (name == "hard") return ThresholdMode::hard;
    if (name == "soft") return ThresholdMode::soft;
    if (name == "garrote" || name == "garotte") return ThresholdMode::garrote;
    if (name == "greater") return ThresholdMode::greater;
    if (name == "less") return ThresholdMode::less;
    return std::nullopt;
}

std::vector<double> threshold(std::span<const double> x, double value, ThresholdMode mode, double substitute) {
    std::vector<double> y(x.size());
    std::transform(x.begin(), x.end(), y.begin(), [&](double v) { return apply(v, value, mode, substitute); });
    return y;
}

std::vector<double> threshold_firm(std::span<const double> x, double low, double high) {
    std::vector<double> y(x.size());
    for (std::size_t k = 0; k < x.size(); ++k) {
        const double m = std::abs(x[k]);
        if (m > high || std::isnan(m)) y[k] = x[k];
        else if (high <= low) y[k] = 0.0;
        else y[k] = shrink(x[k], high * (1 - low / m) / (high - low));
    }
    return y;
}

}  // namespace signals::core
