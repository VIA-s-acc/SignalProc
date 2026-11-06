#pragma once

#include <cstddef>
#include <string>
#include <span>
#include <vector>

#include "core/bank.hpp"
#include "core/error.hpp"
#include "core/extension.hpp"
#include "core/progress.hpp"

namespace signals::core {

struct DwtOptions {
    Extension mode = Extension::symmetric;
    bool pywt_detail_sign = false;
};

template <class T>
struct BasicDwtLevel {
    std::vector<T> approx, detail;
};

namespace detail {

template <class T>
std::vector<T> signed_copy(const std::vector<T>& v, bool negate) {
    std::vector<T> r = v;
    if (negate)
        for (auto& c : r) c = -c;
    return r;
}

template <class T>
Result<void> check_layout(const BasicFilterBank<T>& bank) {
    const auto n = bank.h0.coeffs.size();
    for (const auto* f : {&bank.h0, &bank.h1, &bank.f0, &bank.f1})
        if (f->coeffs.size() != n || f->offset != 0)
            return make_error(Errc::invalid_argument, "dwt: the four filters must share one length and offset 0");
    if (n == 0 || n % 2 != 0) return make_error(Errc::invalid_argument, "dwt: filter length must be even and positive");
    return {};
}

}  // namespace detail

inline Error cancelled_error(std::string_view what) { return {Errc::cancelled, std::string{what} + ": cancelled"}; }

template <class T>
Result<BasicDwtLevel<T>> dwt_level(std::span<const T> x, const BasicFilterBank<T>& bank, const DwtOptions& options,
                                   const ProgressSink* progress = nullptr) {
    if (x.empty()) return make_error(Errc::invalid_argument, "dwt: empty signal");
    if (auto ok = detail::check_layout(bank); !ok) return std::unexpected{ok.error()};
    if (x.size() == 1 && (options.mode == Extension::reflect || options.mode == Extension::antireflect))
        return make_error(Errc::invalid_argument, "dwt: signal length must be greater than 1 for [anti]reflect mode");
    const auto& lo = bank.h0.coeffs;
    const auto hi = detail::signed_copy(bank.h1.coeffs, options.pywt_detail_sign);
    const auto filter_len = static_cast<std::ptrdiff_t>(lo.size());
    BasicDwtLevel<T> out;

    if (options.mode == Extension::periodization) {
        std::vector<T> xp(x.begin(), x.end());
        if (xp.size() % 2 == 1) xp.push_back(xp.back());
        const auto m = static_cast<std::ptrdiff_t>(xp.size());
        out.approx.assign(xp.size() / 2, T(0));
        out.detail.assign(xp.size() / 2, T(0));
        for (std::ptrdiff_t k = 0; k < m / 2; ++k) {
            if (stop_requested(progress, static_cast<std::size_t>(k))) return std::unexpected{cancelled_error("dwt")};
            const auto base = 2 * k + filter_len / 2;
            T a(0), d(0);
            for (std::ptrdiff_t j = 0; j < filter_len; ++j) {
                const auto& s = xp[static_cast<std::size_t>(detail::floor_mod(base - j, m))];
                a += lo[static_cast<std::size_t>(j)] * s;
                d += hi[static_cast<std::size_t>(j)] * s;
            }
            out.approx[static_cast<std::size_t>(k)] = a;
            out.detail[static_cast<std::size_t>(k)] = d;
        }
        return out;
    }

    const auto n = static_cast<std::ptrdiff_t>(x.size());
    const auto len = dwt_coeff_len(x.size(), lo.size(), options.mode);
    out.approx.assign(len, T(0));
    out.detail.assign(len, T(0));
    for (std::size_t i = 0; i < len; ++i) {
        if (stop_requested(progress, i)) return std::unexpected{cancelled_error("dwt")};
        const auto top = 2 * static_cast<std::ptrdiff_t>(i) + 1;
        const bool interior = top - (filter_len - 1) >= 0 && top < n;
        T a(0), d(0);
        for (std::ptrdiff_t k = 0; k < filter_len; ++k) {
            const T s = interior ? x[static_cast<std::size_t>(top - k)] : extended_sample(x, top - k, options.mode);
            a += lo[static_cast<std::size_t>(k)] * s;
            d += hi[static_cast<std::size_t>(k)] * s;
        }
        out.approx[i] = a;
        out.detail[i] = d;
    }
    return out;
}

template <class T>
Result<std::vector<T>> idwt_level(std::span<const T> approx, std::span<const T> detail, const BasicFilterBank<T>& bank,
                                  const DwtOptions& options, const ProgressSink* progress = nullptr) {
    if (approx.size() != detail.size())
        return make_error(Errc::length_mismatch, "idwt: approximation and detail lengths differ");
    if (approx.empty()) return make_error(Errc::invalid_argument, "idwt: empty coefficients");
    if (auto ok = detail::check_layout(bank); !ok) return std::unexpected{ok.error()};
    const auto& lo = bank.f0.coeffs;
    const auto hi = detail::signed_copy(bank.f1.coeffs, options.pywt_detail_sign);
    const auto filter_len = static_cast<std::ptrdiff_t>(lo.size());
    const auto n = static_cast<std::ptrdiff_t>(approx.size());

    if (options.mode == Extension::periodization) {
        const auto m = 2 * n;
        std::vector<T> w(static_cast<std::size_t>(m), T(0));
        for (std::ptrdiff_t k = 0; k < n; ++k) {
            if (stop_requested(progress, static_cast<std::size_t>(k))) return std::unexpected{cancelled_error("idwt")};
            for (std::ptrdiff_t j = 0; j < filter_len; ++j)
                w[static_cast<std::size_t>(detail::floor_mod(2 * k + j, m))] +=
                    lo[static_cast<std::size_t>(j)] * approx[static_cast<std::size_t>(k)] +
                    hi[static_cast<std::size_t>(j)] * detail[static_cast<std::size_t>(k)];
        }
        std::vector<T> out(static_cast<std::size_t>(m), T(0));
        for (std::ptrdiff_t i = 0; i < m; ++i)
            out[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(detail::floor_mod(i + filter_len / 2 - 1, m))];
        return out;
    }

    const auto out_len = 2 * n - filter_len + 2;
    if (out_len <= 0) return make_error(Errc::invalid_argument, "idwt: too few coefficients for this filter");
    std::vector<T> out(static_cast<std::size_t>(out_len), T(0));
    for (std::ptrdiff_t i = 0; i < out_len; ++i) {
        if (stop_requested(progress, static_cast<std::size_t>(i))) return std::unexpected{cancelled_error("idwt")};
        const auto pos = i + filter_len - 2;
        T acc(0);
        for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, (pos - filter_len + 2) / 2); k < n; ++k) {
            const auto j = pos - 2 * k;
            if (j < 0) break;
            if (j >= filter_len) continue;
            acc += lo[static_cast<std::size_t>(j)] * approx[static_cast<std::size_t>(k)] +
                   hi[static_cast<std::size_t>(j)] * detail[static_cast<std::size_t>(k)];
        }
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

}  // namespace signals::core
