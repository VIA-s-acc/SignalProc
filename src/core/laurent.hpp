#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace signals::core {

template <class T>
struct BasicLaurent {
    std::vector<T> coeffs;
    std::ptrdiff_t offset = 0;

    std::ptrdiff_t first() const { return offset; }
    std::ptrdiff_t last() const { return offset + static_cast<std::ptrdiff_t>(coeffs.size()) - 1; }
    T at(std::ptrdiff_t n) const {
        const auto k = n - offset;
        if (k < 0 || k >= static_cast<std::ptrdiff_t>(coeffs.size())) return T{0};
        return coeffs[static_cast<std::size_t>(k)];
    }
    bool operator==(const BasicLaurent&) const = default;
};

using Laurent = BasicLaurent<double>;

template <class T>
BasicLaurent<T> multiply(const BasicLaurent<T>& a, const BasicLaurent<T>& b) {
    if (a.coeffs.empty() || b.coeffs.empty()) return {{}, a.offset + b.offset};
    std::vector<T> c(a.coeffs.size() + b.coeffs.size() - 1, T{0});
    for (std::size_t i = 0; i < a.coeffs.size(); ++i)
        for (std::size_t j = 0; j < b.coeffs.size(); ++j) c[i + j] += a.coeffs[i] * b.coeffs[j];
    return {std::move(c), a.offset + b.offset};
}

template <class T>
BasicLaurent<T> scale(const BasicLaurent<T>& p, const T& s) {
    BasicLaurent<T> r = p;
    for (auto& c : r.coeffs) c *= s;
    return r;
}

template <class T>
BasicLaurent<T> add(const BasicLaurent<T>& a, const BasicLaurent<T>& b) {
    if (a.coeffs.empty()) return b;
    if (b.coeffs.empty()) return a;
    const auto lo = std::min(a.first(), b.first());
    const auto hi = std::max(a.last(), b.last());
    BasicLaurent<T> r{std::vector<T>(static_cast<std::size_t>(hi - lo + 1), T{0}), lo};
    for (auto n = lo; n <= hi; ++n) r.coeffs[static_cast<std::size_t>(n - lo)] = a.at(n) + b.at(n);
    return r;
}

template <class T>
BasicLaurent<T> subtract(const BasicLaurent<T>& a, const BasicLaurent<T>& b) {
    return add(a, scale(b, T{-1}));
}

template <class T>
BasicLaurent<T> power(const BasicLaurent<T>& p, int k) {
    BasicLaurent<T> r{{T{1}}, 0};
    for (int i = 0; i < k; ++i) r = multiply(r, p);
    return r;
}

template <class T>
BasicLaurent<T> shift(const BasicLaurent<T>& p, std::ptrdiff_t k) {
    return {p.coeffs, p.offset + k};
}

template <class T>
BasicLaurent<T> alternate(const BasicLaurent<T>& p) {
    BasicLaurent<T> r = p;
    for (std::size_t k = 0; k < r.coeffs.size(); ++k)
        if (((p.offset + static_cast<std::ptrdiff_t>(k)) & 1) != 0) r.coeffs[k] = -r.coeffs[k];
    return r;
}

template <class T>
BasicLaurent<T> reverse(const BasicLaurent<T>& p) {
    return {std::vector<T>(p.coeffs.rbegin(), p.coeffs.rend()), -p.last()};
}

template <class T>
BasicLaurent<T> trim(const BasicLaurent<T>& p, const T& tol) {
    using std::abs;
    std::size_t lo = 0, hi = p.coeffs.size();
    while (lo < hi && !(abs(p.coeffs[lo]) > tol)) ++lo;
    while (hi > lo && !(abs(p.coeffs[hi - 1]) > tol)) --hi;
    if (lo == hi) return {};
    return {std::vector<T>(p.coeffs.begin() + static_cast<std::ptrdiff_t>(lo),
                           p.coeffs.begin() + static_cast<std::ptrdiff_t>(hi)),
            p.offset + static_cast<std::ptrdiff_t>(lo)};
}

template <class To, class From>
BasicLaurent<To> convert(const BasicLaurent<From>& p) {
    BasicLaurent<To> r{{}, p.offset};
    r.coeffs.reserve(p.coeffs.size());
    for (const auto& c : p.coeffs) r.coeffs.push_back(static_cast<To>(c));
    return r;
}

std::complex<double> evaluate(const Laurent& p, double omega);

struct Monomial {
    double coef;
    std::ptrdiff_t exponent;
};

std::optional<Monomial> as_monomial(const Laurent& p, double tol);

}  // namespace signals::core
