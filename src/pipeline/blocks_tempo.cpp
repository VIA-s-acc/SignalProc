#include <cmath>
#include <string>

#include "core/tempo.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

const ParamSpec kMethod{"method", Kind::choice, "vocoder", {"vocoder", "locked", "wsola"}};
const ParamSpec kWindow{"n_fft", Kind::integer, "2048", {}, 16, 65536};
const ParamSpec kHop{"hop", Kind::integer, "512", {}, 1, 32768};

core::Result<core::StretchOptions> stretch_options(std::string_view block, const Params& p) {
    const core::StretchOptions o{core::parse_stretch_method(param_text(p, "method")).value_or(core::StretchMethod::vocoder),
                                 static_cast<std::size_t>(param_int(p, "n_fft")), static_cast<std::size_t>(param_int(p, "hop"))};
    if (o.n_fft % 2 != 0 || o.hop > o.n_fft / 2) return fail(tr("pipeline.error.stretch_window"), fmt::arg("block", block), fmt::arg("n_fft", o.n_fft), fmt::arg("hop", o.hop));
    return o;
}

template <class F>
Out each_channel_progress(const RunContext& ctx, const core::AudioBuffer& a, F&& f) {
    core::AudioBuffer out;
    out.sample_rate = a.sample_rate;
    const auto count = static_cast<double>(std::max<std::size_t>(1, a.channels.size()));
    for (std::size_t c = 0; c < a.channels.size(); ++c) {
        ScaledProgress progress{ctx.progress, static_cast<double>(c) / count, 1 / count};
        if (a.channels[c].empty()) {
            out.channels.emplace_back();
            continue;
        }
        auto r = f(a.channels[c], &progress);
        if (!r) return std::unexpected{r.error()};
        out.channels.push_back(std::move(*r));
    }
    return one(std::move(out));
}

}  // namespace

void register_tempo_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"time_stretch",
                           kIn,
                           kOut,
                           {{"rate", Kind::real, "1.25", {}, core::kMinStretchRate, core::kMaxStretchRate}, kMethod, kWindow, kHop},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto o = stretch_options("time_stretch", p);
                               if (!o) return std::unexpected{o.error()};
                               const double rate = param_real(p, "rate");
                               return each_channel_progress(ctx, audio(in), [&](const Samples& c, core::ProgressSink* progress) { return core::time_stretch(c, rate, *o, progress); });
                           }}));
    out.push_back(checked({"pitch_shift",
                           kIn,
                           kOut,
                           {{"semitones", Kind::real, "3", {}, -core::kMaxShiftSemitones, core::kMaxShiftSemitones}, kMethod, kWindow, kHop},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto o = stretch_options("pitch_shift", p);
                               if (!o) return std::unexpected{o.error()};
                               const double semitones = param_real(p, "semitones");
                               return each_channel_progress(ctx, audio(in), [&](const Samples& c, core::ProgressSink* progress) { return core::pitch_shift(c, semitones, *o, progress); });
                           }}));
    out.push_back(checked({"speed",
                           kIn,
                           kOut,
                           {{"rate", Kind::real, "1.25", {}, core::kMinStretchRate, core::kMaxStretchRate}},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const double rate = param_real(p, "rate");
                               return each_channel_progress(ctx, audio(in), [&](const Samples& c, core::ProgressSink*) -> core::Result<Samples> {
                                   const auto n = static_cast<std::size_t>(std::nearbyint(static_cast<double>(c.size()) / rate));
                                   if (n == 0) return Samples{};
                                   return core::fft_resample(c, n);
                               });
                           }}));
}

}  // namespace signals::pipeline
