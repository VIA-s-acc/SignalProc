#include "core/swt.hpp"

#include <cmath>

namespace signals::core {

std::size_t swt_max_level(std::size_t n) {
    std::size_t j = 0;
    while (n > 0 && n % 2 == 0) {
        n /= 2;
        ++j;
    }
    return j;
}

std::vector<double> swt_energy_weights(std::size_t levels, bool norm) {
    std::vector<double> w(levels + 1, 1.0);
    if (norm) return w;
    w[0] = std::ldexp(1.0, -static_cast<int>(levels));
    for (std::size_t k = 1; k <= levels; ++k) w[k] = std::ldexp(1.0, -static_cast<int>(levels + 1 - k));
    return w;
}

double swt_noise_scale(std::size_t level, bool norm) {
    return norm && level > 1 ? std::pow(2.0, -0.5 * static_cast<double>(level - 1)) : 1.0;
}

}  // namespace signals::core
