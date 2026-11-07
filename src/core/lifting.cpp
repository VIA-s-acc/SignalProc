#include "core/lifting.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <Eigen/Dense>

#include "core/design/families.hpp"

namespace signals::core {

namespace {

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const auto q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

template <class T>
T even_at(std::span<const T> x, std::ptrdiff_t i) {
    const auto n = static_cast<std::ptrdiff_t>(x.size());
    if (i >= n) i = 2 * (n - 1) - i;
    if (i < 0) i = -i;
    return x[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, n - 1))];
}

template <class T>
T clamped(const std::vector<T>& v, std::ptrdiff_t i) {
    return v[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, static_cast<std::ptrdiff_t>(v.size()) - 1))];
}

LiftLevel lift97_with(std::span<const double> x, const Lift97Constants& c) {
    const auto n = x.size();
    LiftLevel out;
    for (std::size_t i = 0; i < n; i += 2) out.s.push_back(x[i]);
    for (std::size_t i = 1; i < n; i += 2) out.d.push_back(x[i]);
    if (out.d.empty()) return out;
    auto& s = out.s;
    auto& d = out.d;
    auto predict = [&](double k) {
        for (std::size_t i = 0; i < d.size(); ++i) d[i] += k * (s[i] + clamped(s, static_cast<std::ptrdiff_t>(i) + 1));
    };
    auto update = [&](double k) {
        for (std::size_t i = 0; i < s.size(); ++i) s[i] += k * (clamped(d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(d, static_cast<std::ptrdiff_t>(i)));
    };
    predict(c.alpha);
    update(c.beta);
    predict(c.gamma);
    update(c.delta);
    for (auto& v : s) v *= c.zeta;
    for (auto& v : d) v /= c.zeta;
    return out;
}

std::array<double, 5> lowpass_taps(const Lift97Constants& c) {
    std::array<double, 5> taps{};
    std::vector<double> x(64, 0.0);
    for (int k = 0; k <= 4; ++k) {
        std::fill(x.begin(), x.end(), 0.0);
        x[static_cast<std::size_t>(32 + k)] = 1;
        taps[static_cast<std::size_t>(k)] = lift97_with(x, c).s[16];
    }
    return taps;
}

}  // namespace

LiftLevel lift53(std::span<const double> x) {
    LiftLevel out;
    const auto n = x.size();
    for (std::size_t i = 0; i < n; i += 2) out.s.push_back(x[i]);
    for (std::size_t i = 1; i < n; i += 2) out.d.push_back(x[i] - 0.5 * (x[i - 1] + even_at(x, static_cast<std::ptrdiff_t>(i) + 1)));
    if (out.d.empty()) return out;
    for (std::size_t i = 0; i < out.s.size(); ++i)
        out.s[i] += 0.25 * (clamped(out.d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(out.d, static_cast<std::ptrdiff_t>(i)));
    return out;
}

std::vector<double> unlift53(const LiftLevel& level) {
    const auto n = level.s.size() + level.d.size();
    std::vector<double> x(n);
    for (std::size_t i = 0; i < level.s.size(); ++i)
        x[2 * i] = level.d.empty() ? level.s[i]
                                   : level.s[i] - 0.25 * (clamped(level.d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(level.d, static_cast<std::ptrdiff_t>(i)));
    for (std::size_t i = 0; i < level.d.size(); ++i) {
        const auto right = 2 * i + 2 < n ? x[2 * i + 2] : x[2 * i];
        x[2 * i + 1] = level.d[i] + 0.5 * (x[2 * i] + right);
    }
    return x;
}

IntLiftLevel lift53_int(std::span<const std::int64_t> x) {
    IntLiftLevel out;
    const auto n = x.size();
    for (std::size_t i = 0; i < n; i += 2) out.s.push_back(x[i]);
    for (std::size_t i = 1; i < n; i += 2) out.d.push_back(x[i] - floor_div(x[i - 1] + even_at(x, static_cast<std::ptrdiff_t>(i) + 1), 2));
    if (out.d.empty()) return out;
    for (std::size_t i = 0; i < out.s.size(); ++i)
        out.s[i] += floor_div(clamped(out.d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(out.d, static_cast<std::ptrdiff_t>(i)) + 2, 4);
    return out;
}

std::vector<std::int64_t> unlift53_int(const IntLiftLevel& level) {
    const auto n = level.s.size() + level.d.size();
    std::vector<std::int64_t> x(n);
    for (std::size_t i = 0; i < level.s.size(); ++i)
        x[2 * i] = level.d.empty() ? level.s[i]
                                   : level.s[i] - floor_div(clamped(level.d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(level.d, static_cast<std::ptrdiff_t>(i)) + 2, 4);
    for (std::size_t i = 0; i < level.d.size(); ++i) {
        const auto right = 2 * i + 2 < n ? x[2 * i + 2] : x[2 * i];
        x[2 * i + 1] = level.d[i] + floor_div(x[2 * i] + right, 2);
    }
    return x;
}

const Lift97Constants& lift97_constants() {
    static const Lift97Constants exact = [] {
        Lift97Constants c{-1.586134342, -0.05298011854, 0.8829110762, 0.4435068522, 1.149604398};
        const auto bank = wavelet("bior4.4");
        if (!bank) return c;
        const auto& h = bank->h0.coeffs;
        const auto centre = static_cast<std::ptrdiff_t>(std::max_element(h.begin(), h.end()) - h.begin());
        std::array<double, 5> target{};
        for (std::ptrdiff_t k = 0; k <= 4; ++k) target[static_cast<std::size_t>(k)] = h[static_cast<std::size_t>(centre + k)];
        for (int iteration = 0; iteration < 8; ++iteration) {
            const auto base = lowpass_taps(c);
            Eigen::Matrix<double, 5, 5> jac;
            Eigen::Matrix<double, 5, 1> r;
            for (int i = 0; i < 5; ++i) r(i) = base[static_cast<std::size_t>(i)] - target[static_cast<std::size_t>(i)];
            if (r.norm() < 1e-16) break;
            for (int j = 0; j < 5; ++j) {
                auto p = c;
                double* field = j == 0 ? &p.alpha : j == 1 ? &p.beta : j == 2 ? &p.gamma : j == 3 ? &p.delta : &p.zeta;
                const double step = 1e-7 * std::max(1.0, std::abs(*field));
                *field += step;
                const auto moved = lowpass_taps(p);
                for (int i = 0; i < 5; ++i) jac(i, j) = (moved[static_cast<std::size_t>(i)] - base[static_cast<std::size_t>(i)]) / step;
            }
            const Eigen::Matrix<double, 5, 1> delta = jac.fullPivLu().solve(r);
            c.alpha -= delta(0);
            c.beta -= delta(1);
            c.gamma -= delta(2);
            c.delta -= delta(3);
            c.zeta -= delta(4);
        }
        return c;
    }();
    return exact;
}

LiftLevel lift97(std::span<const double> x) { return lift97_with(x, lift97_constants()); }

std::vector<double> unlift97(const LiftLevel& level) {
    const auto& c = lift97_constants();
    auto s = level.s;
    auto d = level.d;
    const auto n = s.size() + d.size();
    if (d.empty()) return s;
    for (auto& v : s) v /= c.zeta;
    for (auto& v : d) v *= c.zeta;
    auto update = [&](double k) {
        for (std::size_t i = 0; i < s.size(); ++i) s[i] -= k * (clamped(d, static_cast<std::ptrdiff_t>(i) - 1) + clamped(d, static_cast<std::ptrdiff_t>(i)));
    };
    auto predict = [&](double k) {
        for (std::size_t i = 0; i < d.size(); ++i) d[i] -= k * (s[i] + clamped(s, static_cast<std::ptrdiff_t>(i) + 1));
    };
    update(c.delta);
    predict(c.gamma);
    update(c.beta);
    predict(c.alpha);
    std::vector<double> x(n);
    for (std::size_t i = 0; i < s.size(); ++i) x[2 * i] = s[i];
    for (std::size_t i = 0; i < d.size(); ++i) x[2 * i + 1] = d[i];
    return x;
}

Result<IntLiftPyramid> lift53_int_levels(std::span<const std::int32_t> x, std::size_t levels) {
    if (levels == 0 || levels > 30) return make_error(Errc::level_out_of_range, "lift53: levels must be in 1..30 so that 64-bit coefficients cannot overflow");
    if (x.size() < (std::size_t{1} << levels)) return make_error(Errc::level_out_of_range, "lift53: the signal is shorter than 2^levels samples");
    IntLiftPyramid p;
    std::vector<std::int64_t> approx(x.begin(), x.end());
    std::vector<std::vector<std::int64_t>> details;
    for (std::size_t j = 0; j < levels; ++j) {
        p.lengths.push_back(approx.size());
        auto l = lift53_int(approx);
        details.push_back(std::move(l.d));
        approx = std::move(l.s);
    }
    p.bands.push_back(std::move(approx));
    for (auto it = details.rbegin(); it != details.rend(); ++it) p.bands.push_back(std::move(*it));
    return p;
}

std::vector<std::int64_t> unlift53_int_levels(const IntLiftPyramid& p) {
    if (p.bands.empty()) return {};
    auto approx = p.bands.front();
    for (std::size_t b = 1; b < p.bands.size(); ++b) approx = unlift53_int({std::move(approx), p.bands[b]});
    return approx;
}

}  // namespace signals::core
