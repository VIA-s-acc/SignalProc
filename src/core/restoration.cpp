#include "core/restoration.hpp"

#include <cmath>
#include <complex>
#include <string>

#include "core/analysis.hpp"
#include "core/detail/pocketfft.hpp"

namespace signals::core {
namespace {

using C = std::complex<double>;

void fft(std::vector<C>& a, bool forward) {
    const pocketfft::shape_t shape{a.size()};
    const pocketfft::stride_t stride{sizeof(C)};
    pocketfft::c2c(shape, stride, stride, {0}, forward, a.data(), a.data(), forward ? 1.0 : 1.0 / static_cast<double>(a.size()));
}

}  // namespace

Result<std::vector<double>> wiener_local(std::span<const double> x, std::size_t size, std::optional<double> noise) {
    if (size == 0 || size % 2 == 0) return make_error(Errc::invalid_argument, "wiener: the window size must be odd and positive");
    if (x.empty()) return make_error(Errc::invalid_argument, "wiener: empty signal");
    const auto n = x.size();
    const auto half = static_cast<std::ptrdiff_t>(size / 2);
    std::vector<double> mean(n), var(n);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0, s2 = 0;
        for (std::ptrdiff_t k = -half; k <= half; ++k) {
            const auto j = static_cast<std::ptrdiff_t>(i) + k;
            if (j < 0 || j >= static_cast<std::ptrdiff_t>(n)) continue;
            s += x[static_cast<std::size_t>(j)];
            s2 += x[static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(j)];
        }
        mean[i] = s / static_cast<double>(size);
        var[i] = s2 / static_cast<double>(size) - mean[i] * mean[i];
    }
    double level = 0;
    if (noise) {
        level = *noise;
    } else {
        for (const double v : var) level += v;
        level /= static_cast<double>(n);
    }
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = var[i] < level || !(var[i] > 0) ? mean[i] : (x[i] - mean[i]) * (1 - level / var[i]) + mean[i];
    return out;
}

Result<std::vector<double>> spectral_subtract(std::span<const double> x, std::span<const double> noise, const SpectralSubtraction& o) {
    if (noise.size() < o.n)
        return make_error(Errc::invalid_argument, "spectral subtraction: the noise segment has " + std::to_string(noise.size()) + " samples, fewer than one window of " +
                                                      std::to_string(o.n));
    if (x.size() < o.n) return make_error(Errc::invalid_argument, "spectral subtraction: the signal is shorter than one window of " + std::to_string(o.n));
    if (!(o.alpha >= 0) || !(o.beta >= 0)) return make_error(Errc::invalid_argument, "spectral subtraction: alpha and beta must not be negative");
    auto zn = stft_complex(noise, o.n, o.hop);
    if (!zn) return std::unexpected{zn.error()};
    auto z = stft_complex(x, o.n, o.hop);
    if (!z) return std::unexpected{z.error()};
    const auto bins = o.n / 2 + 1;
    std::vector<double> level(bins, 0.0);
    for (const auto& frame : zn->frames)
        for (std::size_t k = 0; k < bins; ++k) level[k] += std::norm(frame[k]);
    for (auto& v : level) v /= static_cast<double>(zn->frames.size());
    for (auto& frame : z->frames)
        for (std::size_t k = 0; k < bins; ++k) {
            const double power = std::max(std::norm(frame[k]) - o.alpha * level[k], o.beta * level[k]);
            const double magnitude = std::abs(frame[k]);
            frame[k] = magnitude > 0 ? frame[k] * (std::sqrt(power) / magnitude) : C{std::sqrt(power), 0.0};
        }
    auto out = istft(*z);
    if (!out) return std::unexpected{out.error()};
    out->resize(x.size());
    return out;
}

Result<std::vector<double>> wiener_deconvolve(std::span<const double> y, std::span<const double> h, double snr_db) {
    if (h.empty()) return make_error(Errc::invalid_argument, "deconvolve: the kernel is empty");
    if (h.size() > y.size()) return make_error(Errc::invalid_argument, "deconvolve: the kernel is longer than the signal");
    if (!std::isfinite(snr_db)) return make_error(Errc::invalid_argument, "deconvolve: the SNR must be finite");
    const auto n = y.size();
    std::vector<C> Y(y.begin(), y.end()), H(n, C{0, 0});
    for (std::size_t k = 0; k < h.size(); ++k) H[k] = h[k];
    fft(Y, true);
    fft(H, true);
    const double floor = std::pow(10.0, -snr_db / 10);
    for (std::size_t k = 0; k < n; ++k) Y[k] = Y[k] * std::conj(H[k]) / (std::norm(H[k]) + floor);
    fft(Y, false);
    std::vector<double> out(n);
    for (std::size_t k = 0; k < n; ++k) out[k] = Y[k].real();
    return out;
}

}  // namespace signals::core
