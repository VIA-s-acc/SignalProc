#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace signals::core {

enum class Extension { zero, constant, symmetric, reflect, periodic, smooth, periodization, antisymmetric, antireflect };

std::optional<Extension> parse_extension(std::string_view pywt_name);
std::string_view to_string(Extension mode);

std::size_t dwt_coeff_len(std::size_t n, std::size_t filter_len, Extension mode);

namespace detail {

inline std::ptrdiff_t floor_div(std::ptrdiff_t a, std::ptrdiff_t b) {
    std::ptrdiff_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline std::ptrdiff_t floor_mod(std::ptrdiff_t a, std::ptrdiff_t b) { return a - floor_div(a, b) * b; }

}  // namespace detail

// Sample of x extended to index i (any integer) for every mode except periodization,
// which the callers handle by padding x to even length and using periodic.
template <class T>
T extended_sample(std::span<const T> x, std::ptrdiff_t i, Extension mode) {
    const auto n = static_cast<std::ptrdiff_t>(x.size());
    auto at = [&](std::ptrdiff_t k) { return x[static_cast<std::size_t>(k)]; };
    if (i >= 0 && i < n) return at(i);
    switch (mode) {
        case Extension::zero:
            return T{0};
        case Extension::constant:
            return i < 0 ? at(0) : at(n - 1);
        case Extension::periodic:
        case Extension::periodization:
            return at(detail::floor_mod(i, n));
        case Extension::symmetric: {
            const auto m = detail::floor_mod(i, 2 * n);
            return m < n ? at(m) : at(2 * n - 1 - m);
        }
        case Extension::antisymmetric: {
            const auto m = detail::floor_mod(i, 2 * n);
            const T v = m < n ? at(m) : at(2 * n - 1 - m);
            return (detail::floor_div(i, n) & 1) != 0 ? T{-v} : v;
        }
        case Extension::reflect: {
            if (n == 1) return at(0);
            const auto m = detail::floor_mod(i, 2 * n - 2);
            return m < n ? at(m) : at(2 * n - 2 - m);
        }
        case Extension::antireflect: {
            if (n == 1) return at(0);
            const auto period = 2 * n - 2;
            const auto q = detail::floor_div(i, period);
            const auto r = i - q * period;
            const T base = r < n ? at(r) : T{2 * at(n - 1) - at(2 * n - 2 - r)};
            return T{base + T(2 * q) * (at(n - 1) - at(0))};
        }
        case Extension::smooth: {
            if (n == 1) return at(0);
            if (i < 0) return T{at(0) + T(-i) * (at(0) - at(1))};
            return T{at(n - 1) + T(i - n + 1) * (at(n - 1) - at(n - 2))};
        }
    }
    return T{0};
}

template <class T>
std::vector<T> pad(std::span<const T> x, std::size_t before, std::size_t after, Extension mode) {
    std::vector<T> source(x.begin(), x.end());
    if (mode == Extension::periodization && source.size() % 2 == 1) source.push_back(source.back());
    const std::span<const T> s{source};
    std::vector<T> y;
    y.reserve(before + s.size() + after);
    const auto b = static_cast<std::ptrdiff_t>(before);
    const auto total = static_cast<std::ptrdiff_t>(before + s.size() + after);
    for (std::ptrdiff_t k = 0; k < total; ++k) y.push_back(extended_sample(s, k - b, mode));
    return y;
}

}  // namespace signals::core
