#include "core/design/window_fir.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include "core/metrics.hpp"

namespace signals::core {

double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 500; ++k) {
        const double ratio = x / (2.0 * k);
        term *= ratio * ratio;
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

namespace {

double sinc(double x) {
    if (x == 0) return 1.0;
    const double y = std::numbers::pi * x;
    return std::sin(y) / y;
}

}  // namespace

double kaiser_beta(double attenuation_db) {
    const double a = attenuation_db;
    if (a > 50) return 0.1102 * (a - 8.7);
    if (a > 21) return 0.5842 * std::pow(a - 21, 0.4) + 0.07886 * (a - 21);
    return 0.0;
}

KaiserOrder kaiser_order(double attenuation_db, double transition_width) {
    const double a = std::abs(attenuation_db);
    const double taps = (a - 7.95) / 2.285 / (std::numbers::pi * transition_width) + 1;
    return {static_cast<std::size_t>(std::ceil(taps)), kaiser_beta(a)};
}

Laurent kaiser_lowpass(std::size_t taps, double cutoff, double beta) {
    if (taps == 0) return {{}, 0};
    if (taps == 1) return {{1.0}, 0};
    const double alpha = 0.5 * static_cast<double>(taps - 1);
    const double i0_beta = bessel_i0(beta);
    std::vector<double> h(taps);
    for (std::size_t n = 0; n < taps; ++n) {
        const double m = static_cast<double>(n) - alpha;
        const double r = m / alpha;
        const double window = bessel_i0(beta * std::sqrt(1 - r * r)) / i0_beta;
        h[n] = cutoff * sinc(cutoff * m) * window;
    }
    const double dc = neumaier_sum(h);
    for (double& v : h) v /= dc;
    return {std::move(h), 0};
}

}  // namespace signals::core
