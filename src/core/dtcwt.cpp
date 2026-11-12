#include "core/dtcwt.hpp"

#include <algorithm>
#include <cstdint>

namespace signals::core::dtcwt {
namespace {

using Index = std::ptrdiff_t;

Index reflect(Index i, Index r) {
    const Index period = 2 * r, m = ((i % period) + period) % period;
    return m < r ? m : period - 1 - m;
}

std::vector<double> gather(std::span<const double> x, Index from, Index to) {
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(std::max<Index>(0, to - from)));
    for (Index i = from; i < to; ++i) out.push_back(x[static_cast<std::size_t>(reflect(i, static_cast<Index>(x.size())))]);
    return out;
}

std::vector<double> valid_convolve(const std::vector<double>& x, std::span<const double> h) {
    const auto m = h.size();
    if (x.size() < m) return {};
    std::vector<double> y(x.size() - m + 1, 0.0);
    for (std::size_t k = 0; k < y.size(); ++k)
        for (std::size_t i = 0; i < m; ++i) y[k] += h[i] * x[k + m - 1 - i];
    return y;
}

std::vector<double> every_other(std::span<const double> h, std::size_t first) {
    std::vector<double> out;
    for (std::size_t i = first; i < h.size(); i += 2) out.push_back(h[i]);
    return out;
}

double dot(std::span<const double> a, std::span<const double> b) {
    double s = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) s += a[i] * b[i];
    return s;
}

std::vector<double> colfilter(std::span<const double> x, std::span<const double> h) {
    const auto r = static_cast<Index>(x.size()), m2 = static_cast<Index>(h.size() / 2);
    return valid_convolve(gather(x, -m2, r + m2), h);
}

std::vector<double> picked(const std::vector<double>& extended, Index from, Index step, std::size_t count) {
    std::vector<double> out(count);
    for (std::size_t k = 0; k < count; ++k) out[k] = extended[static_cast<std::size_t>(from + step * static_cast<Index>(k))];
    return out;
}

std::vector<double> coldfilt(std::span<const double> x, std::span<const double> ha, std::span<const double> hb) {
    const auto r = static_cast<Index>(x.size()), m = static_cast<Index>(ha.size());
    const auto extended = gather(x, -m, r + m);
    const auto hao = every_other(ha, 0), hae = every_other(ha, 1), hbo = every_other(hb, 0), hbe = every_other(hb, 1);
    std::size_t count = 0;
    for (Index t = 5; t < r + 2 * m - 2; t += 4) ++count;
    const auto a = valid_convolve(picked(extended, 4, 4, count), hao), b = valid_convolve(picked(extended, 2, 4, count), hae);
    const auto c = valid_convolve(picked(extended, 5, 4, count), hbo), d = valid_convolve(picked(extended, 3, 4, count), hbe);
    std::vector<double> y(static_cast<std::size_t>(r / 2), 0.0);
    const bool same = dot(ha, hb) > 0;
    for (std::size_t k = 0; k < a.size(); ++k) {
        y[2 * k + (same ? 0 : 1)] = a[k] + b[k];
        y[2 * k + (same ? 1 : 0)] = c[k] + d[k];
    }
    return y;
}

std::vector<double> colifilt(std::span<const double> x, std::span<const double> ha, std::span<const double> hb) {
    const auto r = static_cast<Index>(x.size()), m = static_cast<Index>(ha.size()), m2 = m / 2;
    std::vector<double> y(static_cast<std::size_t>(2 * r), 0.0);
    if (std::all_of(x.begin(), x.end(), [](double v) { return v == 0.0; })) return y;
    const auto extended = gather(x, -m2, r + m2);
    const auto hao = every_other(ha, 0), hae = every_other(ha, 1), hbo = every_other(hb, 0), hbe = every_other(hb, 1);
    const bool same = dot(ha, hb) > 0;
    const Index start = m2 % 2 == 0 ? 3 : 2, stop = m2 % 2 == 0 ? r + m : r + m - 1;
    std::size_t count = 0;
    for (Index t = start; t < stop; t += 2) ++count;
    const Index ta = same ? start : start - 1, tb = same ? start - 1 : start;
    std::vector<double> p0, p1, p2, p3;
    if (m2 % 2 == 0) {
        p0 = valid_convolve(picked(extended, tb - 2, 2, count), hae);
        p1 = valid_convolve(picked(extended, ta - 2, 2, count), hbe);
        p2 = valid_convolve(picked(extended, tb, 2, count), hao);
        p3 = valid_convolve(picked(extended, ta, 2, count), hbo);
    } else {
        p0 = valid_convolve(picked(extended, tb, 2, count), hao);
        p1 = valid_convolve(picked(extended, ta, 2, count), hbo);
        p2 = valid_convolve(picked(extended, tb, 2, count), hae);
        p3 = valid_convolve(picked(extended, ta, 2, count), hbe);
    }
    for (std::size_t k = 0; k < p0.size() && 4 * k + 3 < y.size(); ++k) {
        y[4 * k] = p0[k];
        y[4 * k + 1] = p1[k];
        y[4 * k + 2] = p2[k];
        y[4 * k + 3] = p3[k];
    }
    return y;
}

std::vector<std::complex<double>> to_complex(const std::vector<double>& hi) {
    std::vector<std::complex<double>> out(hi.size() / 2);
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = {hi[2 * k], hi[2 * k + 1]};
    return out;
}

std::vector<double> to_real(const std::vector<std::complex<double>>& z) {
    std::vector<double> out(2 * z.size());
    for (std::size_t k = 0; k < z.size(); ++k) out[2 * k] = z[k].real(), out[2 * k + 1] = z[k].imag();
    return out;
}

}  // namespace

const Biort* find_biort(std::string_view name) {
    const auto& table = biort_table();
    const auto it = std::find_if(table.begin(), table.end(), [&](const Biort& b) { return b.name == name; });
    return it == table.end() ? nullptr : &*it;
}

const Qshift* find_qshift(std::string_view name) {
    const auto& table = qshift_table();
    const auto it = std::find_if(table.begin(), table.end(), [&](const Qshift& q) { return q.name == name; });
    return it == table.end() ? nullptr : &*it;
}

Result<Pyramid> forward(std::span<const double> x, std::size_t levels, const Biort& biort, const Qshift& qshift) {
    if (x.empty() || x.size() % 2 != 0) return make_error(Errc::invalid_argument, "dtcwt: the signal length must be even and positive");
    Pyramid p;
    if (levels == 0) {
        p.lowpass.assign(x.begin(), x.end());
        return p;
    }
    p.highpasses.push_back(to_complex(colfilter(x, biort.h1o)));
    auto lo = colfilter(x, biort.h0o);
    for (std::size_t level = 1; level < levels; ++level) {
        if (lo.size() % 4 != 0) {
            lo.insert(lo.begin(), lo.front());
            lo.push_back(lo.back());
        }
        p.highpasses.push_back(to_complex(coldfilt(lo, qshift.h1b, qshift.h1a)));
        lo = coldfilt(lo, qshift.h0b, qshift.h0a);
    }
    p.lowpass = std::move(lo);
    return p;
}

Result<std::vector<double>> inverse(const Pyramid& pyramid, const Biort& biort, const Qshift& qshift) {
    if (pyramid.highpasses.empty()) return pyramid.lowpass;
    const auto bad = [] { return make_error(Errc::invalid_argument, "dtcwt: the band lengths do not fit together"); };
    auto lo = pyramid.lowpass;
    for (std::size_t level = pyramid.highpasses.size() - 1; level >= 1; --level) {
        const auto& band = pyramid.highpasses[level];
        if (lo.size() != 2 * band.size() || lo.size() % 2 != 0) return bad();
        const auto hi = to_real(band);
        auto low = colifilt(lo, qshift.g0b, qshift.g0a);
        const auto high = colifilt(hi, qshift.g1b, qshift.g1a);
        if (low.size() != high.size()) return bad();
        for (std::size_t k = 0; k < low.size(); ++k) low[k] += high[k];
        if (low.size() != 2 * pyramid.highpasses[level - 1].size()) {
            if (low.size() < 2) return bad();
            low = std::vector<double>(low.begin() + 1, low.end() - 1);
        }
        if (low.size() != 2 * pyramid.highpasses[level - 1].size()) return bad();
        lo = std::move(low);
    }
    const auto hi = to_real(pyramid.highpasses.front());
    if (lo.size() != hi.size()) return bad();
    auto z = colfilter(lo, biort.g0o);
    const auto high = colfilter(hi, biort.g1o);
    if (z.size() != high.size()) return bad();
    for (std::size_t k = 0; k < z.size(); ++k) z[k] += high[k];
    return z;
}

}  // namespace signals::core::dtcwt
