#include "io/generators.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

#include "core/detail/pocketfft.hpp"
#include "core/metrics.hpp"

namespace signals::io {
namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

double phase(double freq, std::size_t k, double fs) { return kTwoPi * freq * static_cast<double>(k) / fs; }

double sgn(double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }

void scale_to_rms(std::vector<double>& x, double rms) {
    if (x.empty()) return;
    const double current = std::sqrt(core::energy(x) / static_cast<double>(x.size()));
    if (current == 0) return;
    for (double& v : x) v *= rms / current;
}

void shape_spectrum(std::vector<double>& x, double exponent) {
    const std::size_t n = x.size();
    std::vector<std::complex<double>> spectrum(n / 2 + 1);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)};
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, x.data(), spectrum.data(), 1.0);
    spectrum[0] = 0;
    for (std::size_t k = 1; k < spectrum.size(); ++k) spectrum[k] *= std::pow(static_cast<double>(k), exponent);
    pocketfft::c2r(shape, complex_stride, real_stride, 0, pocketfft::BACKWARD, spectrum.data(), x.data(),
                   1.0 / static_cast<double>(n));
}

constexpr std::array kDjPositions{.1, .13, .15, .23, .25, .4, .44, .65, .76, .78, .81};
constexpr std::array kBlocksHeights{4.0, -5.0, 3.0, -4.0, 5.0, -4.2, 2.1, 4.3, -3.1, 2.1, -4.2};
constexpr std::array kBumpsHeights{4.0, 5.0, 3.0, 4.0, 5.0, 4.2, 2.1, 4.3, 3.1, 5.1, 4.2};
constexpr std::array kBumpsWidths{.005, .005, .006, .01, .01, .03, .01, .01, .005, .008, .005};

double donoho_johnstone_at(DonohoJohnstone kind, double t) {
    double f = 0;
    switch (kind) {
    case DonohoJohnstone::blocks:
        for (std::size_t j = 0; j < kDjPositions.size(); ++j) f += kBlocksHeights[j] * (1 + sgn(t - kDjPositions[j])) / 2;
        return f;
    case DonohoJohnstone::bumps:
        for (std::size_t j = 0; j < kDjPositions.size(); ++j)
            f += kBumpsHeights[j] / std::pow(1 + std::abs((t - kDjPositions[j]) / kBumpsWidths[j]), 4);
        return f;
    case DonohoJohnstone::heavisine: return 4 * std::sin(4 * std::numbers::pi * t) - sgn(t - 0.3) - sgn(0.72 - t);
    case DonohoJohnstone::doppler: return std::sqrt(t * (1 - t)) * std::sin(kTwoPi * 1.05 / (t + 0.05));
    }
    return f;
}

}  // namespace

std::vector<double> sine(double freq_hz, double amplitude, double phase0, double fs, std::size_t n, const core::ProgressSink* progress) {
    std::vector<double> x;
    x.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (core::stop_requested(progress, k)) return x;
        x.push_back(amplitude * std::sin(phase(freq_hz, k, fs) + phase0));
    }
    return x;
}

std::vector<double> sweep(double f0, double f1, SweepKind kind, double fs, std::size_t n, const core::ProgressSink* progress) {
    std::vector<double> x(n, 1.0);
    if (n < 2) return x;
    const double t1 = static_cast<double>(n - 1) / fs;
    for (std::size_t k = 0; k < n; ++k) {
        if (core::stop_requested(progress, k)) return x;
        const double t = static_cast<double>(k) / fs;
        double ph = 0;
        if (kind == SweepKind::linear) {
            const double beta = (f1 - f0) / t1;
            ph = kTwoPi * (f0 * t + 0.5 * beta * t * t);
        } else if (f0 == f1) {
            ph = kTwoPi * f0 * t;
        } else {
            const double beta = t1 / std::log(f1 / f0);
            ph = kTwoPi * beta * f0 * (std::pow(f1 / f0, t / t1) - 1.0);
        }
        x[k] = std::cos(ph);
    }
    return x;
}

std::vector<double> impulse(std::size_t n, std::size_t at) {
    std::vector<double> x(n, 0.0);
    if (at < n) x[at] = 1.0;
    return x;
}

std::vector<double> step(std::size_t n, std::size_t at) {
    std::vector<double> x(n, 0.0);
    for (std::size_t k = at; k < n; ++k) x[k] = 1.0;
    return x;
}

std::vector<double> noise(NoiseColor color, double rms, std::size_t n, std::uint64_t seed, const core::ProgressSink* progress) {
    std::mt19937_64 rng{seed};
    std::normal_distribution<double> normal{0.0, 1.0};
    std::vector<double> x;
    x.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (core::stop_requested(progress, k)) return x;
        x.push_back(normal(rng));
    }
    if (n > 0 && color != NoiseColor::white) shape_spectrum(x, color == NoiseColor::pink ? -0.5 : -1.0);
    scale_to_rms(x, rms);
    return x;
}

std::vector<double> harmonics(double f0, std::span<const double> amplitudes, double fs, std::size_t n, const core::ProgressSink* progress) {
    std::vector<double> x(n, 0.0);
    for (std::size_t h = 0; h < amplitudes.size(); ++h) {
        const double freq = static_cast<double>(h + 1) * f0;
        for (std::size_t k = 0; k < n; ++k) {
            if (core::stop_requested(progress, k)) return x;
            x[k] += amplitudes[h] * std::sin(phase(freq, k, fs));
        }
    }
    return x;
}

std::vector<double> am(double carrier, double modulator, double depth, double fs, std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k)
        x[k] = (1 + depth * std::sin(phase(modulator, k, fs))) * std::sin(phase(carrier, k, fs));
    return x;
}

std::vector<double> fm(double carrier, double modulator, double index, double fs, std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = std::sin(phase(carrier, k, fs) + index * std::sin(phase(modulator, k, fs)));
    return x;
}

std::vector<double> donoho_johnstone(DonohoJohnstone kind, std::size_t n) {
    std::vector<double> x(n);
    const double dt = 1.0 / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = donoho_johnstone_at(kind, dt + static_cast<double>(i) * dt);
    return x;
}

}  // namespace signals::io
