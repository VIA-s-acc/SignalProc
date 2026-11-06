#include "core/wavelet/cascade.hpp"

#include <cmath>
#include <numeric>

namespace signals::core {

Result<Cascade> cascade(const Laurent& h, int iterations) {
    if (iterations < 2) return make_error(Errc::invalid_argument, "cascade: need at least 2 iterations");
    const auto t = trim(h, 0.0);
    const double sum = std::accumulate(t.coeffs.begin(), t.coeffs.end(), 0.0);
    if (t.coeffs.empty() || std::abs(sum) < 1e-12) return make_error(Errc::invalid_argument, "cascade: filter sum is zero");
    std::vector<double> c(t.coeffs.size());
    for (std::size_t k = 0; k < c.size(); ++k) c[k] = 2 * t.coeffs[k] / sum;
    std::vector<double> phi{1.0};
    double previous = 0, last = 0;
    for (int i = 0; i < iterations; ++i) {
        std::vector<double> next(2 * phi.size() - 1 + c.size() - 1, 0.0);
        for (std::size_t n = 0; n < phi.size(); ++n)
            for (std::size_t k = 0; k < c.size(); ++k) next[2 * n + k] += phi[n] * c[k];
        phi = std::move(next);
        double energy = 0;
        for (double v : phi) energy += v * v;
        previous = last;
        last = std::sqrt(energy / std::ldexp(1.0, i + 1));
    }
    Cascade out;
    out.phi = std::move(phi);
    out.dt = std::ldexp(1.0, -iterations);
    out.support = static_cast<double>(c.size() - 1);
    out.norm_ratio = previous > 0 ? last / previous : 0;
    return out;
}

}  // namespace signals::core
