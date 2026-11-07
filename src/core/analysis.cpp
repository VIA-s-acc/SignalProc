#include "core/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "core/detail/pocketfft.hpp"
#include "core/freq.hpp"

namespace signals::core {
namespace {

using C = std::complex<double>;
constexpr std::size_t kDirectLimit = std::size_t{1} << 16;

std::vector<C> fft(std::vector<C> a, bool forward) {
    if (a.empty()) return a;
    const pocketfft::shape_t shape{a.size()};
    const pocketfft::stride_t stride{sizeof(C)};
    pocketfft::c2c(shape, stride, stride, {0}, forward, a.data(), a.data(), forward ? 1.0 : 1.0 / static_cast<double>(a.size()));
    return a;
}

std::vector<double> full_correlation(std::span<const double> x, std::span<const double> y) {
    const auto n = x.size() + y.size() - 1;
    std::vector<double> out(n, 0.0);
    if (x.size() * y.size() <= kDirectLimit) {
        for (std::size_t i = 0; i < x.size(); ++i)
            for (std::size_t m = 0; m < y.size(); ++m) out[i + y.size() - 1 - m] += x[i] * y[m];
        return out;
    }
    std::vector<C> a(n), b(n);
    for (std::size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    for (std::size_t m = 0; m < y.size(); ++m) b[y.size() - 1 - m] = y[m];
    a = fft(std::move(a), true);
    b = fft(std::move(b), true);
    for (std::size_t k = 0; k < n; ++k) a[k] *= b[k];
    a = fft(std::move(a), false);
    for (std::size_t k = 0; k < n; ++k) out[k] = a[k].real();
    return out;
}

std::vector<double> hann(std::size_t n) {
    std::vector<double> w(n);
    for (std::size_t k = 0; k < n; ++k) w[k] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n));
    return w;
}

std::vector<double> dct(std::span<const double> x, int type) {
    std::vector<double> out(x.size());
    if (x.empty()) return out;
    const pocketfft::shape_t shape{x.size()};
    const pocketfft::stride_t stride{sizeof(double)};
    pocketfft::dct(shape, stride, stride, {0}, type, x.data(), out.data(), 1.0 / std::sqrt(2.0 * static_cast<double>(x.size())), true);
    return out;
}

}  // namespace

Result<std::vector<double>> correlate(std::span<const double> x, std::span<const double> y, CorrelationMode mode) {
    if (x.empty() || y.empty()) return make_error(Errc::invalid_argument, "correlate: both inputs need samples");
    auto full = full_correlation(x, y);
    std::size_t start = 0, length = full.size();
    if (mode == CorrelationMode::same) {
        length = x.size();
        start = (full.size() - length) / 2;
    } else if (mode == CorrelationMode::valid) {
        length = std::max(x.size(), y.size()) - std::min(x.size(), y.size()) + 1;
        start = std::min(x.size(), y.size()) - 1;
    }
    return std::vector<double>(full.begin() + static_cast<std::ptrdiff_t>(start), full.begin() + static_cast<std::ptrdiff_t>(start + length));
}

std::vector<C> analytic_signal(std::span<const double> x) {
    const auto n = x.size();
    std::vector<C> a(x.begin(), x.end());
    if (n == 0) return a;
    a = fft(std::move(a), true);
    for (std::size_t k = 1; k < n; ++k) {
        if (2 * k < n) a[k] *= 2.0;
        else if (2 * k > n) a[k] = 0.0;
    }
    return fft(std::move(a), false);
}

std::vector<double> envelope(std::span<const double> x) {
    const auto a = analytic_signal(x);
    std::vector<double> out(a.size());
    for (std::size_t n = 0; n < a.size(); ++n) out[n] = std::abs(a[n]);
    return out;
}

std::vector<double> instantaneous_frequency(std::span<const double> x, double fs) {
    const auto a = analytic_signal(x);
    if (a.size() < 2) return {};
    std::vector<double> phase(a.size()), mag(a.size());
    double peak = 0;
    for (std::size_t n = 0; n < a.size(); ++n) {
        phase[n] = std::arg(a[n]);
        mag[n] = std::abs(a[n]);
        peak = std::max(peak, mag[n]);
    }
    phase = unwrap_phase(phase);
    std::vector<double> out(a.size() - 1, 0.0);
    for (std::size_t n = 0; n + 1 < a.size(); ++n) {
        const bool quiet = mag[n] <= 1e-12 * peak || mag[n + 1] <= 1e-12 * peak;
        out[n] = quiet ? (n ? out[n - 1] : 0.0) : (phase[n + 1] - phase[n]) * fs / (2 * std::numbers::pi);
    }
    return out;
}

std::vector<double> real_cepstrum(std::span<const double> x) {
    auto spectrum = fft(std::vector<C>(x.begin(), x.end()), true);
    double peak = 0;
    for (const auto& v : spectrum) peak = std::max(peak, std::abs(v));
    for (auto& v : spectrum) v = std::log(std::abs(v) + 1e-12 * peak);
    const auto back = fft(std::move(spectrum), false);
    std::vector<double> out(back.size());
    for (std::size_t n = 0; n < back.size(); ++n) out[n] = back[n].real();
    return out;
}

std::vector<double> dct2(std::span<const double> x) { return dct(x, 2); }

std::vector<double> idct2(std::span<const double> x) { return dct(x, 3); }

Result<ComplexStft> stft_complex(std::span<const double> x, std::size_t n, std::size_t hop) {
    if (n == 0 || hop == 0 || hop > n) return make_error(Errc::invalid_argument, "stft: the step must be in 1..n");
    const auto half = n / 2;
    std::vector<double> padded(half, 0.0);
    padded.insert(padded.end(), x.begin(), x.end());
    padded.resize(padded.size() + half, 0.0);
    if (padded.size() < n) padded.resize(n, 0.0);
    padded.resize(padded.size() + (hop - (padded.size() - n) % hop) % hop, 0.0);
    const auto window = hann(n);
    double sum = 0;
    for (double w : window) sum += w;
    ComplexStft z;
    z.n = n;
    z.hop = hop;
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)}, complex_stride{sizeof(C)};
    std::vector<double> buffer(n);
    for (std::size_t start = 0; start + n <= padded.size(); start += hop) {
        for (std::size_t k = 0; k < n; ++k) buffer[k] = padded[start + k] * window[k];
        std::vector<C> spectrum(n / 2 + 1);
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0 / sum);
        z.frames.push_back(std::move(spectrum));
    }
    return z;
}

Result<std::vector<double>> istft(const ComplexStft& z) {
    if (z.n == 0 || z.hop == 0 || z.hop > z.n || z.frames.empty()) return make_error(Errc::invalid_argument, "istft: no frames");
    const auto n = z.n;
    const auto window = hann(n);
    double sum = 0;
    for (double w : window) sum += w;
    const auto total = (z.frames.size() - 1) * z.hop + n;
    std::vector<double> out(total, 0.0), norm(total, 0.0), buffer(n);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)}, complex_stride{sizeof(C)};
    for (std::size_t m = 0; m < z.frames.size(); ++m) {
        if (z.frames[m].size() != n / 2 + 1) return make_error(Errc::length_mismatch, "istft: a frame has the wrong number of bins");
        pocketfft::c2r(shape, complex_stride, real_stride, 0, pocketfft::BACKWARD, z.frames[m].data(), buffer.data(), sum / static_cast<double>(n));
        for (std::size_t k = 0; k < n; ++k) {
            out[m * z.hop + k] += buffer[k] * window[k];
            norm[m * z.hop + k] += window[k] * window[k];
        }
    }
    for (std::size_t i = 0; i < total; ++i)
        if (norm[i] > 1e-10) out[i] /= norm[i];
    const auto half = n / 2;
    if (total <= 2 * half) return std::vector<double>{};
    return std::vector<double>(out.begin() + static_cast<std::ptrdiff_t>(half), out.end() - static_cast<std::ptrdiff_t>(half));
}

Result<Thd> thd(std::span<const double> x, double fs, int harmonics) {
    constexpr std::size_t kLobe = 4;
    const auto n = x.size();
    if (n < 64 || !(fs > 0) || harmonics < 2) return make_error(Errc::invalid_argument, "thd: need at least 64 samples, fs > 0 and two harmonics");
    std::vector<double> buffer(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = 2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        buffer[k] = x[k] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
    }
    std::vector<C> spectrum(n / 2 + 1);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)}, complex_stride{sizeof(C)};
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
    std::vector<double> power(spectrum.size());
    for (std::size_t k = 0; k < power.size(); ++k) power[k] = std::norm(spectrum[k]);
    const auto top = power.size() - 1;
    if (top <= 2 * kLobe + 2) return make_error(Errc::invalid_argument, "thd: the signal is too short");
    const auto peak = static_cast<std::size_t>(std::max_element(power.begin() + kLobe + 1, power.end() - 1) - power.begin());
    if (!(power[peak] > 0)) return make_error(Errc::invalid_argument, "thd: no tone in the signal");
    const double a = std::log(power[peak - 1] + 1e-300), b = std::log(power[peak]), c = std::log(power[peak + 1] + 1e-300);
    const double denom = a - 2 * b + c;
    const double position = static_cast<double>(peak) + (denom != 0 ? 0.5 * (a - c) / denom : 0.0);
    auto band = [&](double at) {
        const auto centre = static_cast<std::ptrdiff_t>(std::llround(at));
        double sum = 0;
        for (auto j = std::max<std::ptrdiff_t>(0, centre - static_cast<std::ptrdiff_t>(kLobe));
             j <= std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(top), centre + static_cast<std::ptrdiff_t>(kLobe)); ++j)
            sum += power[static_cast<std::size_t>(j)];
        return sum;
    };
    Thd r;
    r.f0 = position * fs / static_cast<double>(n);
    const double fundamental = band(position);
    double rest = 0;
    for (int h = 2; h <= harmonics; ++h) {
        const double at = position * h;
        if (at + static_cast<double>(kLobe) > static_cast<double>(top)) break;
        const double p = band(at);
        rest += p;
        r.harmonic_db.push_back(10 * std::log10(std::max(p, 1e-300) / fundamental));
    }
    r.thd = std::sqrt(rest / fundamental);
    r.thd_db = 20 * std::log10(std::max(r.thd, 1e-15));
    return r;
}

std::vector<double> onsets(std::span<const double> x, double fs, std::size_t n, std::size_t hop) {
    if (n == 0 || hop == 0 || x.size() < n || !(fs > 0)) return {};
    const auto window = hann(n);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)}, complex_stride{sizeof(C)};
    std::vector<double> buffer(n), previous(n / 2 + 1, 0.0), flux;
    std::vector<C> spectrum(n / 2 + 1);
    for (std::size_t start = 0; start + n <= x.size(); start += hop) {
        for (std::size_t k = 0; k < n; ++k) buffer[k] = x[start + k] * window[k];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        double f = 0;
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            const double m = std::abs(spectrum[k]);
            f += std::max(0.0, m - previous[k]);
            previous[k] = m;
        }
        flux.push_back(f);
    }
    const double peak = *std::max_element(flux.begin(), flux.end());
    if (!(peak > 1e-12)) return {};
    const auto half = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(0.25 * fs / static_cast<double>(hop))));
    const auto gap = static_cast<std::size_t>(std::ceil(0.05 * fs / static_cast<double>(hop)));
    std::vector<double> out, around;
    std::optional<std::size_t> last;
    for (std::size_t m = 0; m < flux.size(); ++m) {
        const bool rising = (m == 0 || flux[m] >= flux[m - 1]) && (m + 1 == flux.size() || flux[m] > flux[m + 1]);
        if (!rising) continue;
        around.assign(flux.begin() + static_cast<std::ptrdiff_t>(m > half ? m - half : 0),
                      flux.begin() + static_cast<std::ptrdiff_t>(std::min(flux.size(), m + half + 1)));
        std::nth_element(around.begin(), around.begin() + static_cast<std::ptrdiff_t>(around.size() / 2), around.end());
        const double threshold = 1.5 * around[around.size() / 2] + 0.1 * peak;
        if (flux[m] <= threshold || (last && m - *last < gap)) continue;
        out.push_back((static_cast<double>(m * hop) + static_cast<double>(n) / 2) / fs);
        last = m;
    }
    return out;
}

}  // namespace signals::core
