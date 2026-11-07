#include "core/wavelet/spectra.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace signals::core {
namespace {

constexpr double kPi = std::numbers::pi;

std::complex<double> dtft(const Laurent& h, double w) {
    if (h.coeffs.empty()) return {0, 0};
    const auto z = std::polar(1.0, -w);
    std::complex<double> s{h.coeffs.back(), 0};
    for (std::size_t k = h.coeffs.size() - 1; k-- > 0;) s = s * z + h.coeffs[k];
    return s * std::polar(1.0, -w * static_cast<double>(h.offset));
}

Result<std::complex<double>> dc_gain(const Laurent& lowpass) {
    const auto at_one = dtft(lowpass, 0);
    if (std::abs(at_one) < 1e-12) return make_error(Errc::invalid_argument, "H(1) = 0: the lowpass has no DC gain, so m0 = H/H(1) is undefined");
    if (std::abs(dtft(lowpass, kPi)) > 1e-6 * std::abs(at_one))
        return make_error(Errc::invalid_argument, "H(-1) != 0: without a zero at z = -1 the product of m0(w/2^j) does not decay and phi is not in L2");
    return at_one;
}

std::vector<std::complex<double>> product(const Laurent& lowpass, std::complex<double> gain, std::span<const double> omega, std::size_t factors) {
    double top = 0;
    for (double w : omega) top = std::max(top, std::abs(w));
    const auto count = factors > 0 ? factors : spectrum_factors(top);
    double moment = 0, sum = 0;
    for (std::size_t n = 0; n < lowpass.coeffs.size(); ++n) {
        moment += static_cast<double>(lowpass.offset + static_cast<std::ptrdiff_t>(n)) * lowpass.coeffs[n];
        sum += lowpass.coeffs[n];
    }
    // The dropped factors multiply to exp(-i·centre·w·2^-count) up to O(w²·4^-count); without it the phase of a
    // truncated product is off by ~1e-10 at w = 8π.
    const double centre = factors > 0 ? 0.0 : moment / sum;
    std::vector<std::complex<double>> out(omega.size());
    for (std::size_t k = 0; k < omega.size(); ++k) {
        std::complex<double> p = std::polar(1.0, -centre * std::ldexp(omega[k], -static_cast<int>(count)));
        for (std::size_t j = 1; j <= count; ++j) p *= dtft(lowpass, std::ldexp(omega[k], -static_cast<int>(j))) / gain;
        out[k] = p;
    }
    return out;
}

}  // namespace

std::size_t spectrum_factors(double omega_max) {
    const double w = std::max(std::abs(omega_max), 1.0);
    return static_cast<std::size_t>(std::clamp(std::ceil(std::log2(w / 1e-9)), 1.0, 64.0));
}

Result<std::vector<std::complex<double>>> scaling_spectrum(const Laurent& lowpass, std::span<const double> omega, std::size_t factors) {
    const auto gain = dc_gain(lowpass);
    if (!gain) return std::unexpected{gain.error()};
    return product(lowpass, *gain, omega, factors);
}

Result<std::vector<std::complex<double>>> wavelet_spectrum(const Laurent& lowpass, const Laurent& highpass, std::span<const double> omega,
                                                           std::size_t factors) {
    const auto gain = dc_gain(lowpass);
    if (!gain) return std::unexpected{gain.error()};
    std::vector<double> half(omega.begin(), omega.end());
    for (auto& w : half) w /= 2;
    auto out = product(lowpass, *gain, half, factors);
    for (std::size_t k = 0; k < half.size(); ++k) out[k] *= dtft(highpass, half[k]) / *gain;
    return out;
}

std::vector<std::complex<double>> continuous_spectrum(const ContinuousWavelet& cw, std::span<const double> omega) {
    std::vector<std::complex<double>> out(omega.size());
    const double root = std::sqrt(2 * kPi);
    for (std::size_t k = 0; k < omega.size(); ++k) {
        const double w = omega[k];
        switch (cw.family) {
        case ContinuousWavelet::Family::morl: out[k] = root / 2 * (std::exp(-(w - 5) * (w - 5) / 2) + std::exp(-(w + 5) * (w + 5) / 2)); break;
        case ContinuousWavelet::Family::mexh: out[k] = 2 / (std::sqrt(3.0) * std::pow(kPi, 0.25)) * root * w * w * std::exp(-w * w / 2); break;
        case ContinuousWavelet::Family::cmor: {
            const double d = w - 2 * kPi * cw.center;
            out[k] = std::exp(-cw.bandwidth * d * d / 4);
            break;
        }
        }
    }
    return out;
}

}  // namespace signals::core
