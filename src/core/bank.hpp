#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/check.hpp"
#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

// pywt layout: all four filters have offset 0 and one even length L; P0 = F0*H0 is centred at L-1.
template <class T>
struct BasicFilterBank {
    std::string name;
    BasicLaurent<T> h0, h1, f0, f1;

    std::size_t length() const { return h0.coeffs.size(); }
};

using FilterBank = BasicFilterBank<double>;

namespace detail {

template <class T>
std::optional<std::ptrdiff_t> halfband_delay(const BasicLaurent<T>& p0, double tol) {
    using std::abs;
    const T t = std::is_floating_point_v<T> ? T(tol) : T(0);
    const auto d = trim(subtract(p0, alternate(p0)), t);
    if (d.coeffs.size() != 1 || abs(d.coeffs.front() - T(2)) > t) return std::nullopt;
    return d.offset;
}

template <class T>
BasicLaurent<T> place(const BasicLaurent<T>& trimmed, std::size_t start, std::size_t length) {
    BasicLaurent<T> r{std::vector<T>(length, T(0)), 0};
    for (std::size_t k = 0; k < trimmed.coeffs.size(); ++k) r.coeffs[start + k] = trimmed.coeffs[k];
    return r;
}

}  // namespace detail

template <class T>
Result<BasicFilterBank<T>> make_bank(std::string name, BasicLaurent<T> h0, BasicLaurent<T> f0, double pr_tol = 1e-12) {
    const auto delay = detail::halfband_delay(multiply(f0, h0), pr_tol);
    if (!delay) return make_error(Errc::not_perfect_reconstruction, name + ": P0(z) - P0(-z) is not 2 z^-l");
    const auto th = trim(h0, T(0));
    const auto tf = trim(f0, T(0));
    const auto nh = static_cast<std::ptrdiff_t>(th.coeffs.size());
    const auto nf = static_cast<std::ptrdiff_t>(tf.coeffs.size());
    const auto ell_rel = *delay - th.offset - tf.offset;
    auto length = std::max(nh, nf);
    length += length % 2;
    const auto limit = 4 * (nh + nf) + 4;
    std::ptrdiff_t sh = 0, sf = -1;
    for (; length <= limit; length += 2) {
        sh = (length - nh + 1) / 2;
        sf = length - 1 - ell_rel - sh;
        if (sf >= 0 && sf + nf <= length) break;
    }
    if (length > limit) return make_error(Errc::invalid_argument, name + ": cannot lay out filters");
    BasicFilterBank<T> b;
    b.name = std::move(name);
    b.h0 = detail::place(th, static_cast<std::size_t>(sh), static_cast<std::size_t>(length));
    b.f0 = detail::place(tf, static_cast<std::size_t>(sf), static_cast<std::size_t>(length));
    b.h1 = alternate(b.f0);
    b.f1 = scale(alternate(b.h0), T(-1));
    return b;
}

template <class T>
BasicFilterBank<T> bank_from_pywt(std::string name, std::vector<T> dec_lo, std::vector<T> dec_hi, std::vector<T> rec_lo,
                                  std::vector<T> rec_hi) {
    BasicFilterBank<T> b;
    b.name = std::move(name);
    b.h0 = {std::move(dec_lo), 0};
    b.f0 = {std::move(rec_lo), 0};
    b.h1 = scale(BasicLaurent<T>{std::move(dec_hi), 0}, T(-1));
    b.f1 = scale(BasicLaurent<T>{std::move(rec_hi), 0}, T(-1));
    return b;
}

struct PrReport {
    CheckResult alias;
    CheckResult distortion;
    std::optional<std::ptrdiff_t> delay;
};

PrReport check_pr(const FilterBank& bank, double tol = 1e-12);
CheckResult check_orthogonality(const FilterBank& bank, double tol);
bool is_linear_phase(const Laurent& h, double tol);

}  // namespace signals::core
