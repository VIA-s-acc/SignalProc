#include "core/spectral.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

#include "core/detail/pocketfft.hpp"
#include "core/metrics.hpp"

namespace signals::core {

Result<std::vector<double>> welch_psd(std::span<const double> x, double fs, std::size_t segment, std::size_t overlap,
                                      ProgressSink* progress) {
    if (segment < 2 || overlap >= segment || x.size() < segment || !(fs > 0))
        return make_error(Errc::invalid_argument, "welch: need 2 <= segment <= signal length and overlap < segment");
    std::vector<double> window(segment);
    for (std::size_t n = 0; n < segment; ++n)
        window[n] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(segment));
    double w2 = 0;
    for (double w : window) w2 += w * w;
    const double scale = 1.0 / (fs * w2);
    const std::size_t step = segment - overlap, segments = 1 + (x.size() - segment) / step, bins = segment / 2 + 1;
    std::vector<double> psd(bins, 0.0), buffer(segment);
    std::vector<std::complex<double>> spectrum(bins);
    const pocketfft::shape_t shape{segment};
    const pocketfft::stride_t real_stride{sizeof(double)};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)};
    for (std::size_t s = 0; s < segments; ++s) {
        if (progress && progress->cancelled()) return make_error(Errc::cancelled, "welch: cancelled");
        const auto part = x.subspan(s * step, segment);
        const double mean = neumaier_sum(part) / static_cast<double>(segment);
        for (std::size_t n = 0; n < segment; ++n) buffer[n] = (part[n] - mean) * window[n];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        for (std::size_t k = 0; k < bins; ++k) psd[k] += std::norm(spectrum[k]) * scale;
        if (progress) progress->report(static_cast<double>(s + 1) / static_cast<double>(segments));
    }
    const std::size_t last = segment % 2 == 0 ? bins - 1 : bins;
    for (std::size_t k = 0; k < bins; ++k) {
        psd[k] /= static_cast<double>(segments);
        if (k > 0 && k < last) psd[k] *= 2;
    }
    return psd;
}

std::vector<double> welch_frequencies(double fs, std::size_t segment) {
    std::vector<double> f(segment / 2 + 1);
    for (std::size_t k = 0; k < f.size(); ++k) f[k] = static_cast<double>(k) * fs / static_cast<double>(segment);
    return f;
}

Result<std::size_t> stft_frames(std::span<const double> x, std::size_t n, std::size_t hop, const StftFrame& out, ProgressSink* progress) {
    if (n < 2 || hop == 0 || x.size() < n) return make_error(Errc::invalid_argument, "stft: need a window of at least 2 samples, no longer than the signal, and a step above 0");
    std::vector<double> window(n);
    double sum = 0;
    for (std::size_t k = 0; k < n; ++k) {
        window[k] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n));
        sum += window[k];
    }
    const double scale = 1.0 / (sum * sum);
    const std::size_t frames = 1 + (x.size() - n) / hop, bins = n / 2 + 1;
    std::vector<double> buffer(n), power(bins);
    std::vector<std::complex<double>> spectrum(bins);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)}, complex_stride{sizeof(std::complex<double>)};
    for (std::size_t f = 0; f < frames; ++f) {
        if (progress && (f & 63) == 0) {
            if (progress->cancelled()) return make_error(Errc::cancelled, "stft: cancelled");
            progress->report(static_cast<double>(f) / static_cast<double>(frames));
        }
        for (std::size_t k = 0; k < n; ++k) buffer[k] = x[f * hop + k] * window[k];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        for (std::size_t b = 0; b < bins; ++b) power[b] = std::norm(spectrum[b]) * scale;
        out(f, power);
    }
    return frames;
}

Result<StftPower> stft_power(std::span<const double> x, std::size_t n, std::size_t hop, ProgressSink* progress) {
    StftPower s;
    s.n = n;
    s.hop = hop;
    s.bins = n / 2 + 1;
    auto frames = stft_frames(x, n, hop, [&](std::size_t, std::span<const double> p) { s.power.insert(s.power.end(), p.begin(), p.end()); }, progress);
    if (!frames) return std::unexpected{frames.error()};
    s.frames = *frames;
    return s;
}

std::vector<Peak> find_peaks(std::span<const double> y, double prominence, std::size_t distance) {
    std::vector<std::size_t> peaks;
    const auto n = y.size();
    for (std::size_t i = 1; i + 1 < n;) {
        if (y[i - 1] < y[i]) {
            auto ahead = i + 1;
            while (ahead + 1 < n && y[ahead] == y[i]) ++ahead;
            if (y[ahead] < y[i]) {
                peaks.push_back((i + ahead - 1) / 2);
                i = ahead;
                continue;
            }
        }
        ++i;
    }
    if (distance > 1 && peaks.size() > 1) {
        std::vector<std::size_t> order(peaks.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::ranges::stable_sort(order, {}, [&](std::size_t i) { return y[peaks[i]]; });
        std::vector<bool> keep(peaks.size(), true);
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const auto j = *it;
            if (!keep[j]) continue;
            for (auto k = j; k-- > 0 && peaks[j] - peaks[k] < distance;) keep[k] = false;
            for (auto k = j + 1; k < peaks.size() && peaks[k] - peaks[j] < distance; ++k) keep[k] = false;
        }
        std::vector<std::size_t> kept;
        for (std::size_t i = 0; i < peaks.size(); ++i)
            if (keep[i]) kept.push_back(peaks[i]);
        peaks = std::move(kept);
    }
    std::vector<Peak> out;
    for (const auto p : peaks) {
        double left = y[p], right = y[p];
        for (auto i = p; i-- > 0 && y[i] <= y[p];) left = std::min(left, y[i]);
        for (auto i = p + 1; i < n && y[i] <= y[p]; ++i) right = std::min(right, y[i]);
        const double prom = y[p] - std::max(left, right);
        if (prom >= prominence) out.push_back({p, prom});
    }
    return out;
}

Note note_of(double f) {
    static constexpr std::array<const char*, 12> names{"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
    if (!(f > 0)) return {"—", 0};
    const double m = 69 + 12 * std::log2(f / 440);
    const auto k = static_cast<long long>(std::nearbyint(m));
    const auto octave = (k >= 0 ? k / 12 : (k - 11) / 12) - 1;
    return {std::string{names[static_cast<std::size_t>(((k % 12) + 12) % 12)]} + std::to_string(octave),
            static_cast<int>(std::nearbyint(100 * (m - static_cast<double>(k))))};
}

std::optional<double> estimate_f0(std::span<const double> f, std::span<const double> db, std::span<const Peak> peaks, double limit_hz) {
    std::optional<std::size_t> best;
    for (const auto& p : peaks)
        if (p.index < f.size() && f[p.index] < limit_hz && (!best || db[p.index] > db[*best])) best = p.index;
    return best ? std::optional{f[*best]} : std::nullopt;
}

std::vector<Harmonic> harmonics(std::span<const double> f, std::span<const double> db, double f0, int count) {
    std::vector<Harmonic> out;
    for (int k = 1; k <= count; ++k) {
        const double target = k * f0;
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < f.size() && i < db.size(); ++i)
            if (f[i] > target * 0.98 && f[i] < target * 1.02 && (!best || db[i] > db[*best])) best = i;
        if (best) out.push_back({k, f[*best], db[*best]});
    }
    return out;
}

}  // namespace signals::core
