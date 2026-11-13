#include "core/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "core/wavedec.hpp"

namespace signals::core {
namespace {

class Neumaier {
public:
    void add(double v) {
        const double t = sum_ + v;
        if (std::abs(sum_) >= std::abs(v))
            comp_ += (sum_ - t) + v;
        else
            comp_ += (v - t) + sum_;
        sum_ = t;
    }
    double value() const { return sum_ + comp_; }

private:
    double sum_ = 0.0;
    double comp_ = 0.0;
};

double squared_difference_sum(std::span<const double> a, std::span<const double> b) {
    Neumaier acc;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = a[i] - b[i];
        acc.add(d * d);
    }
    return acc.value();
}

}  // namespace

double neumaier_sum(std::span<const double> x) {
    Neumaier acc;
    for (double v : x) acc.add(v);
    return acc.value();
}

double energy(std::span<const double> x) {
    Neumaier acc;
    for (double v : x) acc.add(v * v);
    return acc.value();
}

double max_abs_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = std::abs(a[i] - b[i]);
        if (std::isnan(d)) return d;
        if (d > worst) worst = d;
    }
    return worst;
}

double rms_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    if (a.empty()) return 0.0;
    return std::sqrt(squared_difference_sum(a, b) / static_cast<double>(a.size()));
}

double relative_l2_error(std::span<const double> reference, std::span<const double> test) {
    if (reference.size() != test.size()) return std::numeric_limits<double>::infinity();
    return std::sqrt(squared_difference_sum(reference, test) / energy(reference));
}

double snr_db(std::span<const double> reference, std::span<const double> test) {
    if (reference.size() != test.size()) return -std::numeric_limits<double>::infinity();
    const double noise = squared_difference_sum(reference, test);
    if (noise == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(energy(reference) / noise);
}

double psnr_db(std::span<const double> reference, std::span<const double> test, double peak) {
    if (reference.size() != test.size() || reference.empty()) return -std::numeric_limits<double>::infinity();
    const double mse = squared_difference_sum(reference, test) / static_cast<double>(reference.size());
    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(peak * peak / mse);
}

std::vector<double> band_energy_fractions(const BasicCoeffs<double>& coeffs) { return band_energy_fractions(coeffs, {}); }

std::vector<double> band_energy_fractions(const BasicCoeffs<double>& coeffs, std::span<const double> weights) {
    std::vector<double> e;
    e.reserve(coeffs.bands.size());
    for (std::size_t k = 0; k < coeffs.bands.size(); ++k) e.push_back(energy(coeffs.bands[k]) * (k < weights.size() ? weights[k] : 1.0));
    const double total = neumaier_sum(e);
    for (double& v : e) v = total > 0 ? v / total : 0.0;
    return e;
}

std::size_t best_shift(std::span<const double> reference, std::span<const double> test, std::size_t max_shift) {
    const auto n = std::min(reference.size(), test.size());
    const auto last = test.size() > reference.size() ? std::min(test.size() - reference.size(), max_shift) : 0;
    double best = std::numeric_limits<double>::infinity();
    std::size_t shift = 0;
    for (std::size_t s = 0; n > 0 && s <= last; ++s) {
        const double e = max_abs_error(reference.first(n), test.subspan(s, n));
        if (e < best) best = e, shift = s;
    }
    return shift;
}

}  // namespace signals::core
