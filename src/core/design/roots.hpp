#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

#include "core/design/complex.hpp"
#include "core/error.hpp"

namespace signals::core {

template <class Real>
Result<std::vector<Complex<Real>>> polynomial_roots(const std::vector<Real>& coeffs_ascending) {
    using std::abs;
    std::vector<Real> c = coeffs_ascending;
    while (!c.empty() && c.back() == 0) c.pop_back();
    if (c.size() < 2) return std::vector<Complex<Real>>{};
    const std::size_t degree = c.size() - 1;

    double radius = 0.0;
    for (std::size_t i = 1; i <= degree; ++i) {
        const double ratio = std::abs(static_cast<double>(c[degree - i] / c[degree]));
        radius = std::max(radius, std::pow(ratio, 1.0 / static_cast<double>(i)));
    }
    radius = 2.0 * std::max(radius, 1e-3);
    std::vector<Complex<Real>> z(degree);
    for (std::size_t k = 0; k < degree; ++k) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(degree) + 0.4;
        z[k] = {Real(radius * std::cos(angle)), Real(radius * std::sin(angle))};
    }

    const Real eps = std::is_floating_point_v<Real> ? Real(1e-15) : Real(std::ldexp(1.0, -200));
    for (int iteration = 0; iteration < 500; ++iteration) {
        Real worst(0);
        for (std::size_t k = 0; k < degree; ++k) {
            Complex<Real> p{c[degree], Real(0)}, dp{Real(0), Real(0)};
            for (std::size_t i = degree; i-- > 0;) {
                dp = dp * z[k] + p;
                p = p * z[k] + Complex<Real>{c[i], Real(0)};
            }
            if (modulus(dp) == 0) dp = {Real(1e-30), Real(0)};
            const Complex<Real> w = p / dp;
            Complex<Real> s{Real(0), Real(0)};
            for (std::size_t j = 0; j < degree; ++j)
                if (j != k) s = s + Complex<Real>{Real(1), Real(0)} / (z[k] - z[j]);
            const Complex<Real> correction = w / (Complex<Real>{Real(1), Real(0)} - w * s);
            z[k] = z[k] - correction;
            const Real scale = modulus(z[k]) > 1 ? modulus(z[k]) : Real(1);
            const Real relative = modulus(correction) / scale;
            if (relative > worst) worst = relative;
        }
        if (worst < eps) return z;
    }
    return make_error(Errc::no_convergence, "polynomial_roots: Aberth-Ehrlich did not converge in 500 iterations");
}

}  // namespace signals::core
