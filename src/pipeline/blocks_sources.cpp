#include <cmath>
#include <cstdint>
#include <numbers>

#include "io/generators.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

core::Result<std::size_t> source_length(const Params& p) {
    const double samples = param_real(p, "duration") * static_cast<double>(param_int(p, "rate"));
    if (samples > kMaxSamples)
        return fail(tr("pipeline.error.too_long"), fmt::arg("param", "duration"), fmt::arg("value", param_text(p, "duration")));
    return static_cast<std::size_t>(std::llround(samples));
}

Out source(const Params& p, Samples x) {
    const double amplitude = param_real(p, "amplitude");
    for (auto& v : x) v *= amplitude;
    core::AudioBuffer a;
    a.sample_rate = static_cast<double>(param_int(p, "rate"));
    a.channels.push_back(std::move(x));
    return one(std::move(a));
}

double square_wave(double t, double duty) {
    constexpr double pi = std::numbers::pi;
    return std::fmod(t, 2 * pi) < duty * 2 * pi ? 1.0 : -1.0;
}

double sawtooth_wave(double t, double width) {
    constexpr double pi = std::numbers::pi;
    const double tmod = std::fmod(t, 2 * pi);
    if (tmod < width * 2 * pi) return tmod / (pi * width) - 1;
    return (pi * (width + 1) - tmod) / (pi * (1 - width));
}

}  // namespace

void register_source_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"add_noise",
                           kIn,
                           kOut,
                           {{"color", Kind::choice, "white", {"white", "pink", "brown"}}, {"snr_db", Kind::real, "20", {}, -100, 200}, {"seed", Kind::integer, "1", {}, 0, 1e15}},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               double power = 0;
                               std::size_t count = 0;
                               for (const auto& c : a.channels) {
                                   for (const double v : c) power += v * v;
                                   count += c.size();
                               }
                               if (!(power > 0)) return fail(tr("pipeline.error.silent_input"), fmt::arg("block", "add_noise"));
                               const double rms = std::sqrt(power / static_cast<double>(count) / std::pow(10.0, param_real(p, "snr_db") / 10));
                               const auto& name = param_text(p, "color");
                               const auto color = name == "pink" ? io::NoiseColor::pink : name == "brown" ? io::NoiseColor::brown : io::NoiseColor::white;
                               const auto seed = static_cast<std::uint64_t>(param_int(p, "seed"));
                               core::AudioBuffer result;
                               result.sample_rate = a.sample_rate;
                               for (std::size_t ch = 0; ch < a.channels.size(); ++ch) {
                                   auto y = io::noise(color, rms, a.channels[ch].size(), seed + ch, ctx.progress);
                                   if (cancelled(ctx)) return core::make_error(core::Errc::cancelled, "add_noise: cancelled");
                                   for (std::size_t i = 0; i < y.size(); ++i) y[i] += a.channels[ch][i];
                                   result.channels.push_back(std::move(y));
                               }
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"chirp",
                           {},
                           kOut,
                           {{"method", Kind::choice, "linear", {"linear", "log"}},
                            {"f0", Kind::real, "100", {}, 0, 1e6},
                            {"f1", Kind::real, "8000", {}, 0, 1e6},
                            kAmplitude,
                            kRate,
                            kDuration},
                           [](const RunContext& ctx, std::span<const Value>, const Params& p) -> Out {
                               const auto n = source_length(p);
                               if (!n) return std::unexpected{n.error()};
                               const bool log = param_text(p, "method") == "log";
                               const double f0 = param_real(p, "f0"), f1 = param_real(p, "f1");
                               if (log && !(f0 > 0 && f1 > 0)) return fail(tr("pipeline.error.chirp_log"));
                               auto x = io::sweep(f0, f1, log ? io::SweepKind::logarithmic : io::SweepKind::linear, static_cast<double>(param_int(p, "rate")), *n, ctx.progress);
                               if (cancelled(ctx)) return core::make_error(core::Errc::cancelled, "chirp: cancelled");
                               return source(p, std::move(x));
                           }}));
    out.push_back(checked({"osc",
                           {},
                           kOut,
                           {{"kind", Kind::choice, "square", {"square", "sawtooth", "triangle", "pulses"}},
                            {"freq", Kind::real, "440", {}, 0, 1e6},
                            {"duty", Kind::real, "0.5", {}, 0, 1},
                            {"width", Kind::real, "1", {}, 0, 1},
                            kAmplitude,
                            kRate,
                            kDuration},
                           [](const RunContext&, std::span<const Value>, const Params& p) -> Out {
                               const auto n = source_length(p);
                               if (!n) return std::unexpected{n.error()};
                               const auto& kind = param_text(p, "kind");
                               const double f = param_real(p, "freq"), fs = static_cast<double>(param_int(p, "rate"));
                               const double duty = param_real(p, "duty"), width = kind == "triangle" ? 0.5 : param_real(p, "width");
                               Samples x(*n);
                               for (std::size_t k = 0; k < x.size(); ++k) {
                                   const double t = static_cast<double>(k) / fs;
                                   if (kind == "pulses")
                                       x[k] = k == 0 || std::floor(static_cast<double>(k) * f / fs) != std::floor(static_cast<double>(k - 1) * f / fs) ? 1.0 : 0.0;
                                   else if (kind == "square") x[k] = square_wave(2 * std::numbers::pi * f * t, duty);
                                   else x[k] = sawtooth_wave(2 * std::numbers::pi * f * t, width);
                               }
                               return source(p, std::move(x));
                           }}));
}

}  // namespace signals::pipeline
