#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/dwt.hpp"
#include "core/progress.hpp"
#include "core/wavedec.hpp"

namespace signals::core {

struct SwtOptions {
    bool norm = false;
    bool pywt_detail_sign = false;
};

std::size_t swt_max_level(std::size_t n);
std::vector<double> swt_energy_weights(std::size_t levels, bool norm);
double swt_noise_scale(std::size_t level, bool norm);

namespace detail {

template <class T>
BasicFilterBank<T> scaled_bank(const BasicFilterBank<T>& bank, T s) {
    auto out = bank;
    for (auto* f : {&out.h0, &out.h1, &out.f0, &out.f1})
        for (auto& c : f->coeffs) c *= s;
    return out;
}

template <class T>
Result<std::vector<T>> swt_pass(std::span<const T> x, const std::vector<T>& f, std::size_t level, const ProgressSink* progress) {
    const auto n = static_cast<std::ptrdiff_t>(x.size());
    const auto step = std::ptrdiff_t{1} << (level - 1);
    const auto half = static_cast<std::ptrdiff_t>(f.size()) * step / 2;
    std::vector<T> out(x.size(), T(0));
    for (std::ptrdiff_t o = 0; o < n; ++o) {
        if (stop_requested(progress, static_cast<std::size_t>(o))) return std::unexpected{cancelled_error("swt")};
        T sum(0);
        for (std::size_t j = 0; j < f.size(); ++j) {
            auto i = (o + half - static_cast<std::ptrdiff_t>(j) * step) % n;
            if (i < 0) i += n;
            sum += f[j] * x[static_cast<std::size_t>(i)];
        }
        out[static_cast<std::size_t>(o)] = sum;
    }
    return out;
}

}  // namespace detail

template <class T>
Result<BasicCoeffs<T>> swt(std::span<const T> x, const BasicFilterBank<T>& bank, std::size_t levels, const SwtOptions& options,
                           ProgressSink* progress = nullptr) {
    const auto max_level = swt_max_level(x.size());
    if (x.empty() || levels < 1 || levels > max_level)
        return make_error(Errc::level_out_of_range, "swt: levels must be in [1, " + std::to_string(max_level) + "] for n=" + std::to_string(x.size()) +
                                                        " (n must be divisible by 2^levels)");
    if (auto ok = detail::check_layout(bank); !ok) return std::unexpected{ok.error()};
    const T scale = options.norm ? T(1) / std::sqrt(T(2)) : T(1);
    auto lo = bank.h0.coeffs;
    auto hi = detail::signed_copy(bank.h1.coeffs, options.pywt_detail_sign);
    for (auto& c : lo) c *= scale;
    for (auto& c : hi) c *= scale;
    std::vector<T> approx(x.begin(), x.end());
    std::vector<std::vector<T>> details;
    for (std::size_t j = 1; j <= levels; ++j) {
        auto d = detail::swt_pass<T>(approx, hi, j, progress);
        if (!d) return std::unexpected{d.error()};
        auto a = detail::swt_pass<T>(approx, lo, j, progress);
        if (!a) return std::unexpected{a.error()};
        details.push_back(std::move(*d));
        approx = std::move(*a);
        if (progress) progress->report(static_cast<double>(j) / static_cast<double>(levels));
    }
    BasicCoeffs<T> out;
    out.original_length = x.size();
    out.bands.push_back(std::move(approx));
    for (auto it = details.rbegin(); it != details.rend(); ++it) out.bands.push_back(std::move(*it));
    return out;
}

template <class T>
Result<std::vector<T>> iswt(const BasicCoeffs<T>& coeffs, const BasicFilterBank<T>& bank, const SwtOptions& options, ProgressSink* progress = nullptr) {
    if (coeffs.bands.size() < 2) return make_error(Errc::invalid_argument, "iswt: need approximation and details");
    const auto n = coeffs.bands.front().size();
    const auto levels = coeffs.bands.size() - 1;
    for (const auto& b : coeffs.bands)
        if (b.size() != n) return make_error(Errc::length_mismatch, "iswt: all bands must have the same length");
    if (levels > swt_max_level(n)) return make_error(Errc::level_out_of_range, "iswt: too many levels for n=" + std::to_string(n));
    const auto rec = options.norm ? detail::scaled_bank(bank, std::sqrt(T(2))) : bank;
    const DwtOptions dwt{Extension::periodization, options.pywt_detail_sign};
    std::vector<T> output = coeffs.bands.front();
    for (std::size_t j = levels; j >= 1; --j) {
        const std::size_t step = std::size_t{1} << (j - 1);
        const auto& cd = coeffs.bands[levels - j + 1];
        for (std::size_t first = 0; first < step; ++first) {
            if (progress && progress->cancelled()) return make_error(Errc::cancelled, "iswt: cancelled");
            std::vector<std::size_t> indices;
            for (std::size_t i = first; i < n; i += step) indices.push_back(i);
            std::vector<T> ea, ed, oa, od;
            for (std::size_t k = 0; k < indices.size(); ++k) {
                auto& a = k % 2 == 0 ? ea : oa;
                auto& d = k % 2 == 0 ? ed : od;
                a.push_back(output[indices[k]]);
                d.push_back(cd[indices[k]]);
            }
            auto x1 = idwt_level<T>(ea, ed, rec, dwt);
            if (!x1) return std::unexpected{x1.error()};
            auto x2 = idwt_level<T>(oa, od, rec, dwt);
            if (!x2) return std::unexpected{x2.error()};
            const auto m = x2->size();
            for (std::size_t k = 0; k < indices.size() && k < x1->size(); ++k)
                output[indices[k]] = ((*x1)[k] + (*x2)[(k + m - 1) % m]) / T(2);
        }
        if (progress) progress->report(static_cast<double>(levels - j + 1) / static_cast<double>(levels));
    }
    return output;
}

template <class T>
Result<std::vector<std::vector<T>>> mra(std::span<const T> x, const BasicFilterBank<T>& bank, std::size_t levels, const SwtOptions& options) {
    auto c = swt<T>(x, bank, levels, options);
    if (!c) return std::unexpected{c.error()};
    std::vector<std::vector<T>> out;
    for (std::size_t k = 0; k < c->bands.size(); ++k) {
        BasicCoeffs<T> only;
        only.original_length = c->original_length;
        for (std::size_t b = 0; b < c->bands.size(); ++b) only.bands.push_back(b == k ? c->bands[b] : std::vector<T>(c->bands[b].size(), T(0)));
        auto y = iswt<T>(only, bank, options);
        if (!y) return std::unexpected{y.error()};
        out.push_back(std::move(*y));
    }
    return out;
}

}  // namespace signals::core
