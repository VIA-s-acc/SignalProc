#include "core/freq.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include "core/design/roots.hpp"

namespace signals::core {
namespace {

constexpr double kFloorDb = -300.0;
constexpr double kGroupDelayFloor = 1e-12;

std::vector<double> unwrap(const std::vector<double>& p) {
    constexpr double pi = std::numbers::pi, two_pi = 2 * std::numbers::pi;
    std::vector<double> out(p);
    double correction = 0;
    for (std::size_t k = 1; k < p.size(); ++k) {
        const double dd = p[k] - p[k - 1];
        const double shifted = dd + pi;
        double ddmod = shifted - two_pi * std::floor(shifted / two_pi) - pi;
        if (ddmod == -pi && dd > 0) ddmod = pi;
        if (std::abs(dd) >= pi) correction += ddmod - dd;
        out[k] = p[k] + correction;
    }
    return out;
}

}  // namespace

FrequencyResponse frequency_response(const Laurent& h, std::size_t points) {
    FrequencyResponse r;
    r.omega.resize(points);
    r.h.resize(points);
    r.group_delay.resize(points);
    std::vector<std::complex<double>> weighted(points);
    for (std::size_t k = 0; k < points; ++k) {
        const double w = std::numbers::pi * static_cast<double>(k) / static_cast<double>(points);
        r.omega[k] = w;
        std::complex<double> sum{0, 0}, moment{0, 0};
        for (std::size_t j = 0; j < h.coeffs.size(); ++j) {
            const auto n = static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(j));
            const auto term = h.coeffs[j] * std::polar(1.0, -w * n);
            sum += term;
            moment += n * term;
        }
        r.h[k] = sum;
        weighted[k] = moment;
    }
    r.magnitude.resize(points);
    r.magnitude_db.resize(points);
    r.phase.resize(points);
    for (std::size_t k = 0; k < points; ++k) {
        r.magnitude[k] = std::abs(r.h[k]);
        r.magnitude_db[k] = r.magnitude[k] > 0 ? std::max(kFloorDb, 20 * std::log10(r.magnitude[k])) : kFloorDb;
        r.phase[k] = std::arg(r.h[k]);
    }
    r.phase_unwrapped = unwrap(r.phase);
    const double peak = points ? *std::max_element(r.magnitude.begin(), r.magnitude.end()) : 0.0;
    for (std::size_t k = 0; k < points; ++k)
        r.group_delay[k] = r.magnitude[k] > kGroupDelayFloor * peak ? (weighted[k] / r.h[k]).real()
                                                                     : std::numeric_limits<double>::quiet_NaN();
    return r;
}

std::vector<double> step_response(const Laurent& h) {
    std::vector<double> s(h.coeffs.size());
    double acc = 0;
    for (std::size_t k = 0; k < s.size(); ++k) s[k] = acc += h.coeffs[k];
    return s;
}

Result<ZerosPoles> zeros_poles(const Laurent& h) {
    auto first = std::find_if(h.coeffs.begin(), h.coeffs.end(), [](double c) { return c != 0.0; });
    if (first == h.coeffs.end()) return make_error(Errc::invalid_argument, "zeros_poles: filter has no nonzero coefficients");
    auto last = std::find_if(h.coeffs.rbegin(), h.coeffs.rend(), [](double c) { return c != 0.0; }).base();
    const auto offset = h.offset + (first - h.coeffs.begin());
    const std::vector<double> ascending(std::make_reverse_iterator(last), std::make_reverse_iterator(first));
    ZerosPoles out{{}, offset + static_cast<std::ptrdiff_t>(ascending.size()) - 1};
    if (ascending.size() == 1) return out;
    auto roots = polynomial_roots<double>(ascending);
    if (!roots) return std::unexpected{roots.error()};
    for (const auto& z : *roots) out.zeros.emplace_back(z.re, z.im);
    return out;
}

Laurent dilate(const Laurent& h, std::size_t m) {
    if (h.coeffs.empty() || m <= 1) return h;
    Laurent out{std::vector<double>((h.coeffs.size() - 1) * m + 1, 0.0), h.offset * static_cast<std::ptrdiff_t>(m)};
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) out.coeffs[k * m] = h.coeffs[k];
    return out;
}

Result<Laurent> equivalent_filter(const FilterBank& bank, std::size_t level, bool detail) {
    if (level == 0) return make_error(Errc::invalid_argument, "equivalent_filter: level must be at least 1");
    Laurent g{{1.0}, 0};
    for (std::size_t k = 0; k + 1 < level; ++k) g = multiply(g, dilate(bank.h0, std::size_t{1} << k));
    const auto& last = detail ? bank.h1 : bank.h0;
    return multiply(dilate(last, std::size_t{1} << (level - 1)), g);
}

Laurent distortion_transfer(const FilterBank& bank) {
    return scale(add(multiply(bank.f0, bank.h0), multiply(bank.f1, bank.h1)), 0.5);
}

Laurent alias_transfer(const FilterBank& bank) {
    return scale(add(multiply(bank.f0, alternate(bank.h0)), multiply(bank.f1, alternate(bank.h1))), 0.5);
}

BandMetrics band_metrics(const FrequencyResponse& r, double passband_end, double stopband_start) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    BandMetrics m{nan, nan, nan, nan};
    const std::size_t n = r.omega.size();
    if (n == 0) return m;
    const double top = *std::max_element(r.magnitude_db.begin(), r.magnitude_db.end());
    const auto rel = [&](std::size_t k) { return r.magnitude_db[k] - top; };
    const auto crossing = [&](double level, std::size_t from) -> std::optional<std::pair<double, std::size_t>> {
        for (std::size_t k = std::max<std::size_t>(from, 1); k < n; ++k) {
            const double a = rel(k - 1) - level, b = rel(k) - level;
            if ((a >= 0) != (b >= 0)) {
                const double t = a / (a - b);
                return std::pair{(r.omega[k - 1] + t * (r.omega[k] - r.omega[k - 1])) / std::numbers::pi, k};
            }
        }
        return std::nullopt;
    };
    if (auto c = crossing(-10 * std::log10(2.0), 0)) m.cutoff_3db = c->first;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo, stop = -lo;
    for (std::size_t k = 0; k < n; ++k) {
        const double w = r.omega[k] / std::numbers::pi;
        if (w <= passband_end) {
            lo = std::min(lo, rel(k));
            hi = std::max(hi, rel(k));
        }
        if (w >= stopband_start) stop = std::max(stop, rel(k));
    }
    if (hi >= lo) m.passband_ripple_db = hi - lo;
    if (std::isfinite(stop)) m.stopband_attenuation_db = -stop;
    if (auto c1 = crossing(-1.0, 0))
        if (auto c20 = crossing(-20.0, c1->second)) m.transition_width = c20->first - c1->first;
    return m;
}

}  // namespace signals::core
