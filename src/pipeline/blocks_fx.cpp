#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

constexpr double kPi = std::numbers::pi;
constexpr std::array<double, 8> kFreeverbCombs{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<double, 4> kFreeverbAllpasses{556, 441, 341, 225};

std::optional<core::Error> need_rate(std::string_view block, const core::AudioBuffer& a) {
    if (a.sample_rate > 0) return std::nullopt;
    return core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.no_rate")), fmt::arg("block", block))};
}

double phase(double freq, std::size_t k, double fs) { return 2 * kPi * freq * static_cast<double>(k) / fs; }

double lfo(std::string_view shape, double t) {
    if (shape == "sine") return std::sin(t);
    const double wrapped = std::fmod(t, 2 * kPi);
    if (shape == "square") return wrapped < kPi ? 1.0 : -1.0;
    return wrapped < kPi ? wrapped / (kPi * 0.5) - 1 : (kPi * 1.5 - wrapped) / (kPi * 0.5);
}

Samples modulated_delay(const Samples& x, double fs, double rate, double depth_ms, double base_ms, double feedback, double mix) {
    Samples line(x.size()), y(x.size());
    const auto at = [&](double i) { return i >= 0 ? line[static_cast<std::size_t>(i)] : 0.0; };
    for (std::size_t k = 0; k < x.size(); ++k) {
        const double d = std::max(1.0, (base_ms + depth_ms * (1 + std::sin(phase(rate, k, fs))) / 2) * fs / 1000);
        const double pos = static_cast<double>(k) - d;
        const double i0 = std::floor(pos), frac = pos - i0;
        const double wet = (1 - frac) * at(i0) + (frac > 0 ? frac * at(i0 + 1) : 0.0);
        line[k] = x[k] + feedback * wet;
        y[k] = (1 - mix) * x[k] + mix * wet;
    }
    return y;
}

Samples phaser(const Samples& x, double fs, std::size_t stages, double rate, double lo, double hi, double feedback, double mix) {
    Samples xs(stages, 0.0), ys(stages, 0.0), y(x.size());
    double last = 0;
    for (std::size_t k = 0; k < x.size(); ++k) {
        const double fc = lo * std::pow(hi / lo, (1 - std::cos(phase(rate, k, fs))) / 2);
        const double t = std::tan(kPi * fc / fs);
        const double a = (t - 1) / (t + 1);
        double u = x[k] + feedback * last;
        for (std::size_t s = 0; s < stages; ++s) {
            const double v = a * (u - ys[s]) + xs[s];
            xs[s] = u;
            ys[s] = v;
            u = v;
        }
        last = u;
        y[k] = (1 - mix) * x[k] + mix * u;
    }
    return y;
}

std::size_t freeverb_length(double length, double fs) { return static_cast<std::size_t>(std::max(1.0, std::floor(length * fs / 44100 + 0.5))); }

Samples reverb(const Samples& x, double fs, double room, double damping, double wet, double dry) {
    const double feedback = 0.28 * room + 0.7, damp = 0.4 * damping;
    Samples acc(x.size(), 0.0);
    for (const double length : kFreeverbCombs) {
        Samples line(freeverb_length(length, fs), 0.0);
        std::size_t at = 0;
        double store = 0;
        for (std::size_t k = 0; k < x.size(); ++k) {
            const double out = line[at];
            store = out * (1 - damp) + store * damp;
            line[at] = 0.015 * x[k] + store * feedback;
            at = at + 1 == line.size() ? 0 : at + 1;
            acc[k] += out;
        }
    }
    for (const double length : kFreeverbAllpasses) {
        Samples line(freeverb_length(length, fs), 0.0);
        std::size_t at = 0;
        for (std::size_t k = 0; k < x.size(); ++k) {
            const double held = line[at];
            line[at] = acc[k] + held * 0.5;
            acc[k] = held - acc[k];
            at = at + 1 == line.size() ? 0 : at + 1;
        }
    }
    Samples y(x.size());
    for (std::size_t k = 0; k < x.size(); ++k) y[k] = dry * x[k] + wet * 3 * acc[k];
    return y;
}

core::Result<std::size_t> source_length(const Params& p) {
    const double samples = param_real(p, "duration") * static_cast<double>(param_int(p, "rate"));
    if (samples > kMaxSamples) return fail(tr("pipeline.error.too_long"), fmt::arg("param", "duration"), fmt::arg("value", param_text(p, "duration")));
    return static_cast<std::size_t>(std::llround(samples));
}

core::AudioBuffer mono_source(const Params& p, Samples x) {
    const double amplitude = param_real(p, "amplitude");
    for (auto& v : x) v *= amplitude;
    return {static_cast<double>(param_int(p, "rate")), {std::move(x)}};
}

}  // namespace

void register_fx_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"tremolo",
                           kIn,
                           kOut,
                           {{"rate_hz", Kind::real, "5", {}, 0, 20000}, {"depth", Kind::real, "0.5", {}, 0, 1}, {"shape", Kind::choice, "sine", {"sine", "triangle", "square"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("tremolo", a)) return std::unexpected{*e};
                               const double rate = param_real(p, "rate_hz"), depth = param_real(p, "depth");
                               const auto& shape = param_text(p, "shape");
                               return each_channel(a, [&](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t k = 0; k < c.size(); ++k) y[k] = c[k] * (1 - depth * (1 - lfo(shape, phase(rate, k, a.sample_rate))) / 2);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"modulated_delay",
                           kIn,
                           kOut,
                           {{"mode", Kind::choice, "chorus", {"chorus", "flanger", "vibrato"}},
                            {"rate_hz", Kind::real, "0.8", {}, 0, 100},
                            {"depth_ms", Kind::real, "3", {}, 0, 100},
                            {"base_ms", Kind::real, "12", {}, 0, 1000},
                            {"feedback", Kind::real, "0.5", {}, -0.99, 0.99},
                            {"mix", Kind::real, "0.5", {}, 0, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("modulated_delay", a)) return std::unexpected{*e};
                               const auto& mode = param_text(p, "mode");
                               const double feedback = mode == "flanger" ? param_real(p, "feedback") : 0.0;
                               const double mix = mode == "vibrato" ? 1.0 : param_real(p, "mix");
                               return each_channel(a, [&](const Samples& c) {
                                   return modulated_delay(c, a.sample_rate, param_real(p, "rate_hz"), param_real(p, "depth_ms"), param_real(p, "base_ms"), feedback, mix);
                               });
                           }}));
    out.push_back(checked({"phaser",
                           kIn,
                           kOut,
                           {{"stages", Kind::integer, "4", {}, 2, 12},
                            {"rate_hz", Kind::real, "0.5", {}, 0, 100},
                            {"min_hz", Kind::real, "200", {}, 1, 1e6},
                            {"max_hz", Kind::real, "2000", {}, 1, 1e6},
                            {"feedback", Kind::real, "0.5", {}, -0.99, 0.99},
                            {"mix", Kind::real, "0.5", {}, 0, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("phaser", a)) return std::unexpected{*e};
                               const double lo = param_real(p, "min_hz"), hi = param_real(p, "max_hz"), nyquist = a.sample_rate / 2;
                               for (const double f : {lo, hi})
                                   if (!(f < nyquist)) return fail(tr("pipeline.error.cutoff_range"), fmt::arg("block", "phaser"), fmt::arg("f", f), fmt::arg("nyquist", nyquist));
                               const auto stages = static_cast<std::size_t>(param_int(p, "stages"));
                               return each_channel(a, [&](const Samples& c) {
                                   return phaser(c, a.sample_rate, stages, param_real(p, "rate_hz"), lo, hi, param_real(p, "feedback"), param_real(p, "mix"));
                               });
                           }}));
    out.push_back(checked({"reverb",
                           kIn,
                           kOut,
                           {{"room_size", Kind::real, "0.5", {}, 0, 1},
                            {"damping", Kind::real, "0.5", {}, 0, 1},
                            {"wet", Kind::real, "0.33", {}, 0, 1},
                            {"dry", Kind::real, "0.7", {}, 0, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("reverb", a)) return std::unexpected{*e};
                               if (!(*std::ranges::max_element(kFreeverbCombs) * a.sample_rate / 44100 <= double(1 << 20)))
                                   return std::unexpected{core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.rate_too_high")), fmt::arg("block", "reverb"), fmt::arg("rate", a.sample_rate))}};
                               return each_channel(a, [&](const Samples& c) {
                                   return reverb(c, a.sample_rate, param_real(p, "room_size"), param_real(p, "damping"), param_real(p, "wet"), param_real(p, "dry"));
                               });
                           }}));
    out.push_back(checked({"wavefold",
                           kIn,
                           kOut,
                           {{"drive", Kind::real, "2", {}, 0.01, 1000}, {"shape", Kind::choice, "triangle", {"triangle", "sine"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double drive = param_real(p, "drive");
                               const bool sine = param_text(p, "shape") == "sine";
                               return each_sample(in, [drive, sine](double x) {
                                   const double u = drive * x;
                                   if (sine) return std::sin(kPi / 2 * u);
                                   double v = std::fmod(u + 1, 4.0);
                                   if (v < 0) v += 4;
                                   return v < 2 ? v - 1 : 3 - v;
                               });
                           }}));
    out.push_back(checked({"am",
                           kIn,
                           kOut,
                           {{"carrier_hz", Kind::real, "1000", {}, 0, 1e6}, {"depth", Kind::real, "0.5", {}, 0, 100}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("am", a)) return std::unexpected{*e};
                               const double fc = param_real(p, "carrier_hz"), depth = param_real(p, "depth");
                               return each_channel(a, [&](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t k = 0; k < c.size(); ++k) y[k] = (1 + depth * c[k]) * std::cos(phase(fc, k, a.sample_rate));
                                   return y;
                               });
                           }}));
    out.push_back(checked({"fm",
                           kIn,
                           kOut,
                           {{"carrier_hz", Kind::real, "1000", {}, 0, 1e6}, {"deviation_hz", Kind::real, "100", {}, 0, 1e6}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = need_rate("fm", a)) return std::unexpected{*e};
                               const double fc = param_real(p, "carrier_hz"), deviation = param_real(p, "deviation_hz"), fs = a.sample_rate;
                               return each_channel(a, [&](const Samples& c) {
                                   Samples y(c.size());
                                   double integral = 0;
                                   for (std::size_t k = 0; k < c.size(); ++k) {
                                       y[k] = std::cos(phase(fc, k, fs) + 2 * kPi * deviation * integral / fs);
                                       integral += c[k];
                                   }
                                   return y;
                               });
                           }}));
    out.push_back(checked({"gausspulse",
                           {},
                           kOut,
                           {{"fc", Kind::real, "1000", {}, 0.001, 1e6},
                            {"bw", Kind::real, "0.5", {}, 0.001, 100},
                            {"bwr", Kind::real, "-6", {}, -200, -0.001},
                            kAmplitude,
                            kRate,
                            {"duration", Kind::real, "0.01", {}, 0, 3600}},
                           [](const RunContext&, std::span<const Value>, const Params& p) -> Out {
                               const auto n = source_length(p);
                               if (!n) return std::unexpected{n.error()};
                               const double fc = param_real(p, "fc"), bw = param_real(p, "bw"), fs = static_cast<double>(param_int(p, "rate"));
                               const double ref = std::pow(10.0, param_real(p, "bwr") / 20.0);
                               const double a = -(kPi * fc * bw) * (kPi * fc * bw) / (4.0 * std::log(ref));
                               const double centre = *n == 0 ? 0.0 : static_cast<double>(*n - 1) / 2;
                               Samples x(*n);
                               for (std::size_t k = 0; k < x.size(); ++k) {
                                   const double t = (static_cast<double>(k) - centre) / fs;
                                   x[k] = std::exp(-a * t * t) * std::cos(2 * kPi * fc * t);
                               }
                               return one(mono_source(p, std::move(x)));
                           }}));
    out.push_back(checked({"impulse_train",
                           {},
                           kOut,
                           {{"period", Kind::samples, "100"}, {"offset", Kind::samples, "0"}, kAmplitude, kRate, kDuration},
                           [](const RunContext&, std::span<const Value>, const Params& p) -> Out {
                               const auto n = source_length(p);
                               if (!n) return std::unexpected{n.error()};
                               const auto fs = static_cast<double>(param_int(p, "rate"));
                               const auto period = param_samples(p, "period", fs), offset = param_samples(p, "offset", fs);
                               if (!period) return std::unexpected{period.error()};
                               if (!offset) return std::unexpected{offset.error()};
                               if (*period == 0) return fail(tr("pipeline.error.zero_samples"), fmt::arg("block", "impulse_train"), fmt::arg("param", "period"));
                               Samples x(*n, 0.0);
                               for (std::size_t k = *offset; k < x.size(); k += *period) x[k] = 1.0;
                               return one(mono_source(p, std::move(x)));
                           }}));
}

}  // namespace signals::pipeline
