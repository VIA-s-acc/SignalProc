#pragma once

#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

#include "core/bank.hpp"
#include "core/design/maxflat.hpp"
#include "core/scalar.hpp"

namespace signals::core {

namespace detail {

inline Result<BasicLaurent<Rational>> divide_exactly_by_one_plus_w(const BasicLaurent<Rational>& p) {
    if (p.coeffs.size() < 2) return make_error(Errc::invalid_argument, "divide by 1 + w: degree must be at least 1");
    std::vector<Rational> q(p.coeffs.size() - 1);
    Rational carry{0};
    for (std::size_t i = p.coeffs.size(); i-- > 1;) {
        carry = p.coeffs[i] - carry;
        q[i - 1] = carry;
    }
    if (p.coeffs[0] != carry) return make_error(Errc::invalid_argument, "divide by 1 + w: nonzero remainder");
    return BasicLaurent<Rational>{std::move(q), p.offset};
}

template <class T>
BasicLaurent<T> with_sum(const BasicLaurent<Rational>& p, const T& target) {
    Rational sum{0};
    for (const auto& c : p.coeffs) sum += c;
    if constexpr (std::is_same_v<T, Rational>) {
        return scale(p, Rational{target / sum});
    } else {
        return scale(convert<double>(p), target / static_cast<double>(sum));
    }
}

}  // namespace detail

template <class T>
Result<BasicFilterBank<T>> cdf_spline(int nr, int nd) {
    if (nr < 1 || nd < 1 || (nr + nd) % 2 != 0)
        return make_error(Errc::unknown_wavelet, "cdf_spline: orders must be positive with an even sum");
    const int p = (nr + nd) / 2;
    auto q = maxflat_halfband<Rational>(p);
    for (int i = 0; i < 2 * p; ++i) {
        auto divided = detail::divide_exactly_by_one_plus_w(q);
        if (!divided) return std::unexpected{divided.error()};
        q = std::move(*divided);
    }
    const BasicLaurent<Rational> one_plus{{Rational{1}, Rational{1}}, 0};
    const auto h0 = multiply(power(one_plus, nd), q);
    const auto f0 = power(one_plus, nr);
    const std::string name = "bior" + std::to_string(nr) + "." + std::to_string(nd);
    if constexpr (std::is_same_v<T, Rational>) {
        return make_bank<Rational>(name, detail::with_sum<Rational>(h0, Rational{1}), detail::with_sum<Rational>(f0, Rational{2}));
    } else {
        return make_bank<double>(name, detail::with_sum<double>(h0, std::sqrt(2.0)),
                                 detail::with_sum<double>(f0, std::sqrt(2.0)));
    }
}

}  // namespace signals::core
