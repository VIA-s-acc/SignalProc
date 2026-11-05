#include "core/laurent.hpp"

namespace signals::core {

std::complex<double> evaluate(const Laurent& p, double omega) {
    std::complex<double> sum{0.0, 0.0};
    for (std::size_t k = 0; k < p.coeffs.size(); ++k) {
        const auto n = static_cast<double>(p.offset + static_cast<std::ptrdiff_t>(k));
        sum += p.coeffs[k] * std::polar(1.0, -omega * n);
    }
    return sum;
}

std::optional<Monomial> as_monomial(const Laurent& p, double tol) {
    const auto t = trim(p, tol);
    if (t.coeffs.size() != 1) return std::nullopt;
    return Monomial{t.coeffs.front(), t.offset};
}

}  // namespace signals::core
