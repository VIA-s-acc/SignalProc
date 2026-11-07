#include "tui/screens/tempo_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

#include "core/pitch.hpp"

namespace signals::tui {

double next_tempo_rate(double rate, bool up) {
    if (up) {
        for (const double r : kTempoRates)
            if (r > rate + 1e-9) return r;
        return kTempoRates.back();
    }
    for (auto it = kTempoRates.rbegin(); it != kTempoRates.rend(); ++it)
        if (*it < rate - 1e-9) return *it;
    return kTempoRates.front();
}

double expected_ratio(const TempoOptions& o) {
    switch (o.mode) {
    case TempoMode::stretch: return 1.0;
    case TempoMode::shift: return std::pow(2.0, o.semitones / 12);
    case TempoMode::speed: return o.rate;
    }
    return 1.0;
}

double cents_off(double f, double reference) { return f > 0 && reference > 0 ? 1200 * std::log2(f / reference) : 0.0; }

std::vector<double> tempo_source(TempoSource s, double fs, std::size_t n) {
    std::vector<double> x(n);
    double phase = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double f = s == TempoSource::vibrato ? kTempoToneHz * std::pow(2.0, 0.5 / 12 * std::sin(2 * std::numbers::pi * 5 * t)) : kTempoToneHz;
        x[i] = 0.6 * std::sin(phase) + 0.3 * std::sin(2 * phase + 0.4) + 0.15 * std::sin(3 * phase + 1.1);
        phase += 2 * std::numbers::pi * f / fs;
    }
    return x;
}

core::Result<TempoView> compute_tempo(const TempoOptions& o, std::span<const double> x, double fs) {
    if (x.empty() || !(fs > 0)) return core::make_error(core::Errc::invalid_argument, "tempo: no signal");
    const core::StretchOptions stretch{o.method, kTempoWindow, kTempoHop};
    core::Result<std::vector<double>> y = core::make_error(core::Errc::invalid_argument, "tempo: no mode");
    switch (o.mode) {
    case TempoMode::stretch: y = core::time_stretch(x, o.rate, stretch); break;
    case TempoMode::shift: y = core::pitch_shift(x, o.semitones, stretch); break;
    case TempoMode::speed: {
        const auto n = std::max<std::size_t>(1, static_cast<std::size_t>(std::nearbyint(static_cast<double>(x.size()) / o.rate)));
        y = core::fft_resample(x, n);
        break;
    }
    }
    if (!y) return std::unexpected{y.error()};
    const auto frame = std::max(kTempoWindow, std::bit_ceil(2 * static_cast<std::size_t>(std::ceil(fs / 60))));
    const core::YinOptions yin{frame, frame / 4, 60, 1000, 0.1};
    auto f0_in = core::yin_track(x, fs, yin);
    if (!f0_in) return std::unexpected{f0_in.error()};
    auto f0_out = core::yin_track(*y, fs, yin);
    if (!f0_out) return std::unexpected{f0_out.error()};
    TempoView v;
    v.fs = fs;
    v.x.assign(x.begin(), x.end());
    v.y.assign(y->begin(), y->end());
    v.frame_seconds = static_cast<double>(frame) / fs;
    v.hop_seconds = static_cast<double>(frame / 4) / fs;
    v.f0_in_median = core::median_f0(*f0_in);
    v.f0_out_median = core::median_f0(*f0_out);
    v.expected = v.f0_in_median * expected_ratio(o);
    v.f0_in = std::move(*f0_in);
    v.f0_out = std::move(*f0_out);
    v.seconds_in = static_cast<double>(x.size()) / fs;
    v.seconds_out = static_cast<double>(y->size()) / fs;
    return v;
}

}  // namespace signals::tui
