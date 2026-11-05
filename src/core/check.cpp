#include "core/check.hpp"

#include <algorithm>
#include <cmath>

namespace signals::core {

double default_tolerance(std::size_t levels, std::size_t filter_len, double max_abs_x) {
    const double unit_roundoff = std::ldexp(1.0, -53);
    return 100.0 * static_cast<double>(std::max<std::size_t>(levels, 1)) * static_cast<double>(filter_len) *
           unit_roundoff * max_abs_x;
}

}  // namespace signals::core
