#include <cmath>
#include <numbers>

#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

core::Result<Biquad> cookbook(const Params& p, double rate) {
    const double f0 = param_real(p, "f0"), q = param_real(p, "q");
    if (!(f0 > 0 && f0 < rate / 2)) return fail(tr("pipeline.error.cutoff_range"), fmt::arg("block", "biquad"), fmt::arg("f", f0), fmt::arg("nyquist", rate / 2));
    const auto& type = param_text(p, "type");
    const double gain = std::pow(10.0, param_real(p, "gain_db") / 40);
    const double w0 = 2 * std::numbers::pi * f0 / rate, cw = std::cos(w0), alpha = std::sin(w0) / (2 * q);
    const double shelf = 2 * std::sqrt(gain) * alpha;
    double b0 = 0, b1 = 0, b2 = 0, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    if (type == "lowpass") b0 = (1 - cw) / 2, b1 = 1 - cw, b2 = (1 - cw) / 2;
    else if (type == "highpass") b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2;
    else if (type == "bandpass") b0 = alpha, b1 = 0, b2 = -alpha;
    else if (type == "notch") b0 = 1, b1 = -2 * cw, b2 = 1;
    else if (type == "allpass") b0 = 1 - alpha, b1 = -2 * cw, b2 = 1 + alpha;
    else if (type == "peaking") {
        b0 = 1 + alpha * gain, b1 = -2 * cw, b2 = 1 - alpha * gain;
        a0 = 1 + alpha / gain, a2 = 1 - alpha / gain;
    } else if (type == "lowshelf") {
        b0 = gain * ((gain + 1) - (gain - 1) * cw + shelf), b1 = 2 * gain * ((gain - 1) - (gain + 1) * cw), b2 = gain * ((gain + 1) - (gain - 1) * cw - shelf);
        a0 = (gain + 1) + (gain - 1) * cw + shelf, a1 = -2 * ((gain - 1) + (gain + 1) * cw), a2 = (gain + 1) + (gain - 1) * cw - shelf;
    } else {
        b0 = gain * ((gain + 1) + (gain - 1) * cw + shelf), b1 = -2 * gain * ((gain - 1) + (gain + 1) * cw), b2 = gain * ((gain + 1) + (gain - 1) * cw - shelf);
        a0 = (gain + 1) - (gain - 1) * cw + shelf, a1 = 2 * ((gain - 1) - (gain + 1) * cw), a2 = (gain + 1) - (gain - 1) * cw - shelf;
    }
    return Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

Samples biquad_filter(const Biquad& f, const Samples& x) {
    Samples y(x.size());
    double z0 = 0, z1 = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        y[n] = f.b0 * x[n] + z0;
        z0 = f.b1 * x[n] - f.a1 * y[n] + z1;
        z1 = f.b2 * x[n] - f.a2 * y[n];
    }
    return y;
}

double smoothing(double ms, double rate) {
    const double samples = ms / 1000 * rate;
    return samples > 0 ? std::exp(-1 / samples) : 0.0;
}

double sample_at(const Samples& c, std::size_t n) { return n < c.size() ? c[n] : 0.0; }

Samples peak_db(const core::AudioBuffer& a) {
    Samples level(a.frames());
    for (std::size_t n = 0; n < level.size(); ++n) {
        double peak = 0;
        for (const auto& c : a.channels) peak = std::max(peak, std::abs(sample_at(c, n)));
        level[n] = 20 * std::log10(std::max(peak, 1e-10));
    }
    return level;
}

Samples rms_db(const core::AudioBuffer& a, double coef) {
    Samples level(a.frames());
    double power = 0;
    for (std::size_t n = 0; n < level.size(); ++n) {
        double square = 0;
        for (const auto& c : a.channels) square += sample_at(c, n) * sample_at(c, n);
        power = coef * power + (1 - coef) * (square / static_cast<double>(a.channels.size()));
        level[n] = 10 * std::log10(std::max(power, 1e-20));
    }
    return level;
}

double static_curve(double x, double threshold, double slope, double knee) {
    const double over = x - threshold;
    if (knee > 0 && 2 * std::abs(over) <= knee) return x + (slope - 1) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
    if (over > 0) return threshold + over * slope;
    return x;
}

Out apply_gain(const core::AudioBuffer& a, const Samples& gain_db) {
    core::AudioBuffer out;
    out.sample_rate = a.sample_rate;
    for (const auto& c : a.channels) {
        Samples y(c.size());
        for (std::size_t n = 0; n < c.size(); ++n) y[n] = c[n] * std::pow(10.0, gain_db[n] / 20);
        out.channels.push_back(std::move(y));
    }
    return one(std::move(out));
}

Out compress(const core::AudioBuffer& a, const Samples& level, double threshold, double slope, double knee, double makeup, double attack, double release) {
    Samples gain(level.size());
    double state = 0;
    for (std::size_t n = 0; n < level.size(); ++n) {
        const double target = static_curve(level[n], threshold, slope, knee) - level[n];
        const double coef = target < state ? attack : release;
        state = coef * state + (1 - coef) * target;
        gain[n] = state + makeup;
    }
    return apply_gain(a, gain);
}

Out compressor(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    const double rate = a.sample_rate;
    const auto level = param_text(p, "detector") == "rms" ? rms_db(a, smoothing(param_real(p, "rms_ms"), rate)) : peak_db(a);
    return compress(a, level, param_real(p, "threshold_db"), 1 / param_real(p, "ratio"), param_real(p, "knee_db"), param_real(p, "makeup_db"),
                    smoothing(param_real(p, "attack_ms"), rate), smoothing(param_real(p, "release_ms"), rate));
}

Out limiter(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    return compress(a, peak_db(a), param_real(p, "threshold_db"), 0, 0, 0, smoothing(param_real(p, "attack_ms"), a.sample_rate),
                    smoothing(param_real(p, "release_ms"), a.sample_rate));
}

Out gate(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    const auto level = peak_db(a);
    const double threshold = param_real(p, "threshold_db"), closed = -param_real(p, "range_db");
    const double attack = smoothing(param_real(p, "attack_ms"), a.sample_rate), release = smoothing(param_real(p, "release_ms"), a.sample_rate);
    Samples gain(level.size());
    double state = 0;
    for (std::size_t n = 0; n < level.size(); ++n) {
        const double target = level[n] < threshold ? closed : 0.0;
        const double coef = target > state ? attack : release;
        state = coef * state + (1 - coef) * target;
        gain[n] = state;
    }
    return apply_gain(a, gain);
}

Samples delayed_feedback(const Samples& x, std::size_t delay, double feedback) {
    Samples e(x.size(), 0.0);
    for (std::size_t n = delay; n < x.size(); ++n) e[n] = x[n - delay] + feedback * e[n - delay];
    return e;
}

Out echo(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    const double samples = param_real(p, "delay_ms") / 1000 * a.sample_rate;
    if (!(samples <= kMaxSamples)) return fail(tr("pipeline.error.output_too_long"), fmt::arg("n", samples), fmt::arg("factor", 1));
    const auto delay = static_cast<std::size_t>(std::floor(samples + 0.5));
    if (delay == 0) return fail(tr("pipeline.error.delay_short"), fmt::arg("block", "echo"));
    const double feedback = param_real(p, "feedback"), mix = param_real(p, "mix");
    return each_channel(a, [&](const Samples& c) {
        const auto e = delayed_feedback(c, delay, feedback);
        Samples y(c.size());
        for (std::size_t n = 0; n < c.size(); ++n) y[n] = (1 - mix) * c[n] + mix * e[n];
        return y;
    });
}

Out comb(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    const auto delay = param_samples(p, "delay", a.sample_rate);
    if (!delay) return std::unexpected{delay.error()};
    if (*delay == 0) return fail(tr("pipeline.error.delay_short"), fmt::arg("block", "comb"));
    const double g = param_real(p, "gain");
    const bool feedback = param_text(p, "kind") == "feedback";
    if (feedback && !(std::abs(g) < 1)) return fail(tr("pipeline.error.comb_feedback"));
    return each_channel(a, [&](const Samples& c) {
        Samples y(c);
        for (std::size_t n = *delay; n < c.size(); ++n) y[n] = c[n] + g * (feedback ? y[n - *delay] : c[n - *delay]);
        return y;
    });
}

Out mix(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in, 0);
    const double ga = std::pow(10.0, param_real(p, "gain_a_db") / 20), gb = std::pow(10.0, param_real(p, "gain_b_db") / 20);
    const bool fade = param_text(p, "mode") == "crossfade";
    const double start = std::floor(param_real(p, "at") * a.sample_rate + 0.5), length = std::floor(param_real(p, "crossfade_ms") / 1000 * a.sample_rate + 0.5);
    const auto weight = [&](std::size_t n) {
        if (!fade) return -1.0;
        const double t = static_cast<double>(n) - start;
        if (t < 0) return 0.0;
        return length > 0 ? std::min(1.0, t / length) : 1.0;
    };
    return pairwise("mix", a, audio(in, 1), [&](const Samples& x, const Samples& y) -> core::Result<Samples> {
        Samples z(std::max(x.size(), y.size()));
        for (std::size_t n = 0; n < z.size(); ++n) {
            const double u = ga * sample_at(x, n), v = gb * sample_at(y, n), w = weight(n);
            z[n] = w < 0 ? u + v : (1 - w) * u + w * v;
        }
        return z;
    });
}

}  // namespace

void register_effect_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"shift",
                           kIn,
                           kOut,
                           {{"samples", Kind::integer, "0", {}, -kMaxSamples, kMaxSamples}, {"mode", Kind::choice, "zero", {"zero", "circular"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const auto s = param_int(p, "samples");
                               const bool circular = param_text(p, "mode") == "circular";
                               return each_channel(audio(in), [&](const Samples& c) {
                                   const auto n = static_cast<long long>(c.size());
                                   Samples y(c.size(), 0.0);
                                   for (long long i = 0; i < n; ++i) {
                                       const long long j = circular ? ((i + s) % n + n) % n : i + s;
                                       if (j >= 0 && j < n) y[static_cast<std::size_t>(j)] = c[static_cast<std::size_t>(i)];
                                   }
                                   return y;
                               });
                           }}));
    out.push_back(checked({"biquad",
                           kIn,
                           kOut,
                           {{"type", Kind::choice, "lowpass", {"lowpass", "highpass", "bandpass", "notch", "peaking", "lowshelf", "highshelf", "allpass"}},
                            {"f0", Kind::real, "1000", {}, 0, 1e6},
                            {"q", Kind::real, "0.7071", {}, 0.01, 1000},
                            {"gain_db", Kind::real, "0", {}, -60, 60}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto f = cookbook(p, a.sample_rate);
                               if (!f) return std::unexpected{f.error()};
                               return each_channel(a, [&](const Samples& c) { return biquad_filter(*f, c); });
                           }}));
    out.push_back(checked({"compressor",
                           kIn,
                           kOut,
                           {{"detector", Kind::choice, "peak", {"peak", "rms"}},
                            {"threshold_db", Kind::real, "-20", {}, -120, 0},
                            {"ratio", Kind::real, "4", {}, 1, 100},
                            {"knee_db", Kind::real, "0", {}, 0, 48},
                            {"attack_ms", Kind::real, "10", {}, 0, 1000},
                            {"release_ms", Kind::real, "100", {}, 0, 5000},
                            {"makeup_db", Kind::real, "0", {}, -60, 60},
                            {"rms_ms", Kind::real, "10", {}, 0.01, 1000}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return compressor(in, p); }}));
    out.push_back(checked({"limiter",
                           kIn,
                           kOut,
                           {{"threshold_db", Kind::real, "-1", {}, -120, 0}, {"attack_ms", Kind::real, "0", {}, 0, 100}, {"release_ms", Kind::real, "50", {}, 0, 5000}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return limiter(in, p); }}));
    out.push_back(checked({"gate",
                           kIn,
                           kOut,
                           {{"threshold_db", Kind::real, "-50", {}, -200, 0},
                            {"range_db", Kind::real, "80", {}, 0, 200},
                            {"attack_ms", Kind::real, "1", {}, 0, 1000},
                            {"release_ms", Kind::real, "100", {}, 0, 5000}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return gate(in, p); }}));
    out.push_back(checked({"echo",
                           kIn,
                           kOut,
                           {{"delay_ms", Kind::real, "250", {}, 0, 10000}, {"feedback", Kind::real, "0.4", {}, -0.999, 0.999}, {"mix", Kind::real, "0.5", {}, 0, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return echo(in, p); }}));
    out.push_back(checked({"comb",
                           kIn,
                           kOut,
                           {{"kind", Kind::choice, "feedback", {"feedback", "feedforward"}}, {"delay", Kind::samples, "100"}, {"gain", Kind::real, "0.7", {}, -1, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return comb(in, p); }}));
    out.push_back(checked({"mix",
                           kPair,
                           kOut,
                           {{"gain_a_db", Kind::real, "0", {}, -120, 24},
                            {"gain_b_db", Kind::real, "0", {}, -120, 24},
                            {"mode", Kind::choice, "sum", {"sum", "crossfade"}},
                            {"at", Kind::real, "0", {}, 0, 3600},
                            {"crossfade_ms", Kind::real, "10", {}, 0, 60000}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return mix(in, p); }}));
}

}  // namespace signals::pipeline
