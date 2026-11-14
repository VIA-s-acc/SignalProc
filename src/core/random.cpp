#include "core/random.hpp"

#include <cmath>

namespace signals::core {

double uniform53(std::mt19937_64& rng) { return static_cast<double>(rng() >> 11) * 0x1.0p-53; }

double portable_log(double x) {
    constexpr double kLn2Hi = 6.93147180369123816490e-01;
    constexpr double kLn2Lo = 1.90821492927058770002e-10;
    int e = 0;
    double m = std::frexp(x, &e);
    if (m < 0.70710678118654752440) m *= 2, --e;
    const double s = (m - 1) / (m + 1), s2 = s * s;
    double t = 1.0 / 25;
    for (int k = 11; k >= 0; --k) t = t * s2 + 1.0 / (2 * k + 1);
    return e * kLn2Hi + (2 * s * t + e * kLn2Lo);
}

double NormalDraws::next() {
    if (spare_) {
        const double v = *spare_;
        spare_.reset();
        return v;
    }
    for (;;) {
        const double u = 2 * uniform53(rng_) - 1;
        const double v = 2 * uniform53(rng_) - 1;
        const double s = u * u + v * v;
        if (!(s < 1) || s == 0) continue;
        const double f = std::sqrt(-2 * portable_log(s) / s);
        spare_ = v * f;
        return u * f;
    }
}

}  // namespace signals::core
