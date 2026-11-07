#include "core/adaptive.hpp"

#include <cmath>
#include <string>

namespace signals::core {

Result<AdaptiveResult> adaptive_filter(std::span<const double> x, std::span<const double> d, std::size_t taps, double mu, AdaptiveRule rule, double eps) {
    if (x.size() != d.size()) return make_error(Errc::length_mismatch, "adaptive filter: x has " + std::to_string(x.size()) + " samples, d has " + std::to_string(d.size()));
    if (taps == 0 || taps > x.size()) return make_error(Errc::invalid_argument, "adaptive filter: taps must be 1…" + std::to_string(x.size()));
    if (!(mu > 0) || !std::isfinite(mu)) return make_error(Errc::invalid_argument, "adaptive filter: mu must be positive");
    AdaptiveResult r;
    r.w.assign(taps, 0.0);
    r.y.resize(x.size());
    r.e.resize(x.size());
    std::vector<double> u(taps, 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
        for (std::size_t k = taps - 1; k > 0; --k) u[k] = u[k - 1];
        u[0] = x[n];
        double y = 0, power = 0;
        for (std::size_t k = 0; k < taps; ++k) {
            y += r.w[k] * u[k];
            power += u[k] * u[k];
        }
        const double e = d[n] - y;
        if (!std::isfinite(e))
            return make_error(Errc::no_convergence, "adaptive filter: diverged at sample " + std::to_string(n) + "; lower mu (LMS is stable below 2/(taps·power of x))");
        r.y[n] = y;
        r.e[n] = e;
        const double step = rule == AdaptiveRule::nlms ? mu * e / (eps + power) : mu * e;
        for (std::size_t k = 0; k < taps; ++k) r.w[k] += step * u[k];
    }
    return r;
}

}  // namespace signals::core
