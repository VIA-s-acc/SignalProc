#include "core/metrics.hpp"

#include <cmath>
#include <limits>
#include <vector>

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

}  // namespace signals::core
