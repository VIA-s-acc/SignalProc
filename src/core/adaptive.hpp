#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

enum class AdaptiveRule { lms, nlms };

struct AdaptiveResult {
    std::vector<double> y, e, w;
};

Result<AdaptiveResult> adaptive_filter(std::span<const double> x, std::span<const double> d, std::size_t taps, double mu, AdaptiveRule rule,
                                       double eps = 1e-8);

}  // namespace signals::core
