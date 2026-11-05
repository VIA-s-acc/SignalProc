#include "core/design/maxflat.hpp"

#include <stdexcept>

#include "core/design/roots.hpp"

namespace signals::core {

std::vector<Complex<Float256>> maxflat_q_roots(int p) {
    std::vector<Float256> b;
    Float256 binomial{1};
    for (int k = 0; k < p; ++k) {
        if (k > 0) binomial = binomial * (p - 1 + k) / k;
        b.push_back(binomial);
    }
    auto y_roots = polynomial_roots<Float256>(b);
    if (!y_roots) throw std::runtime_error(y_roots.error().message);
    std::vector<Complex<Float256>> z;
    for (const auto& y : *y_roots) {
        const Complex<Float256> c{2 - 4 * y.re, -4 * y.im};
        const auto s = complex_sqrt(c * c - Complex<Float256>{Float256{4}, Float256{0}});
        z.push_back((c + s) / Complex<Float256>{Float256{2}, Float256{0}});
        z.push_back((c - s) / Complex<Float256>{Float256{2}, Float256{0}});
    }
    return z;
}

}  // namespace signals::core
