#include "core/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/analysis.hpp"

namespace signals::core {
namespace {

struct Lags {
    std::size_t window, tau_min, tau_max;
};

// d(tau) is built from energies and an FFT-backed cross term rather than the direct double sum, which would cost
// window * tau_max per frame; YIN only needs d to rounding accuracy, so the cancellation is harmless.
double yin_frame(std::span<const double> seg, const Lags& s, double fs, double threshold) {
    const auto w = s.window, top = s.tau_max;
    const auto cross = correlate(seg.first(w + top), seg.first(w), CorrelationMode::valid);
    if (!cross) return 0.0;
    double e0 = 0;
    for (std::size_t j = 0; j < w; ++j) e0 += seg[j] * seg[j];
    std::vector<double> cmnd(top + 1, 1.0);
    double shifted = e0, acc = 0;
    for (std::size_t tau = 1; tau <= top; ++tau) {
        shifted += seg[tau - 1 + w] * seg[tau - 1 + w] - seg[tau - 1] * seg[tau - 1];
        const double d = std::max(0.0, e0 + shifted - 2 * (*cross)[tau]);
        acc += d;
        cmnd[tau] = acc > 0 ? d * static_cast<double>(tau) / acc : 1.0;
    }
    auto tau = s.tau_min;
    while (tau <= top && !(cmnd[tau] < threshold)) ++tau;
    if (tau > top) return 0.0;
    while (tau + 1 <= top && cmnd[tau + 1] < cmnd[tau]) ++tau;
    double delta = 0;
    if (tau >= 2 && tau + 1 <= top) {
        const double a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
        if (a - 2 * b + c > 0) delta = 0.5 * (a - c) / (a - 2 * b + c);
    }
    return fs / (static_cast<double>(tau) + delta);
}

}  // namespace

Result<std::vector<double>> yin_track(std::span<const double> x, double fs, const YinOptions& o, ProgressSink* progress) {
    if (!(fs > 0) || !std::isfinite(fs)) return make_error(Errc::invalid_argument, "yin: the sample rate must be positive");
    if (!(o.fmin > 0) || !(o.fmin < o.fmax) || !std::isfinite(o.fmax)) return make_error(Errc::invalid_argument, "yin: need 0 < fmin < fmax");
    if (o.frame < 4 || o.hop == 0) return make_error(Errc::invalid_argument, "yin: the frame must hold at least 4 samples and the hop at least 1");
    if (!(o.threshold > 0) || !(o.threshold <= 1)) return make_error(Errc::invalid_argument, "yin: the threshold must be in (0, 1]");
    const Lags lags{o.frame / 2, std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(fs / o.fmax))), 0};
    const double top = std::ceil(fs / o.fmin);
    if (!(top <= static_cast<double>(o.frame - lags.window))) return make_error(Errc::invalid_argument, "yin: the frame is too short for fmin");
    Lags s = lags;
    s.tau_max = static_cast<std::size_t>(top);
    std::vector<double> f0;
    const std::size_t frames = x.size() >= o.frame ? (x.size() - o.frame) / o.hop + 1 : 0;
    f0.reserve(frames);
    for (std::size_t k = 0; k < frames; ++k) {
        if (progress) {
            if (progress->cancelled()) return make_error(Errc::cancelled, "yin: cancelled");
            progress->report(static_cast<double>(k) / static_cast<double>(frames));
        }
        f0.push_back(yin_frame(x.subspan(k * o.hop, o.frame), s, fs, o.threshold));
    }
    return f0;
}

double median_f0(std::span<const double> track) {
    std::vector<double> voiced;
    std::ranges::copy_if(track, std::back_inserter(voiced), [](double f) { return f > 0; });
    if (voiced.empty()) return 0.0;
    std::ranges::sort(voiced);
    const auto h = voiced.size() / 2;
    return voiced.size() % 2 ? voiced[h] : 0.5 * (voiced[h - 1] + voiced[h]);
}

}  // namespace signals::core
