#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

std::vector<double> upfirdn(const Laurent& h, std::span<const double> x, std::size_t up, std::size_t down);

struct MultirateOptions {
    bool anti_alias = true;
    double attenuation_db = 80.0;
    double transition = 0.1;
    bool compensate_delay = true;
};

Laurent anti_alias_filter(std::size_t m, const MultirateOptions& options);

Result<std::vector<double>> decimate(std::span<const double> x, std::size_t m, const MultirateOptions& options);
Result<std::vector<double>> interpolate(std::span<const double> x, std::size_t m, const MultirateOptions& options);

}  // namespace signals::core
