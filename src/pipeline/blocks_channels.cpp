#include <cmath>
#include <numbers>

#include "core/signal_ops.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
using namespace blocks;

void register_channel_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"merge", {{"l", PortType::signal}, {"r", PortType::signal}}, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& l = audio(in, 0);
                               const auto& r = audio(in, 1);
                               if (auto e = same_rate("merge", l, r)) return std::unexpected{*e};
                               core::AudioBuffer result{l.sample_rate, l.channels};
                               result.channels.insert(result.channels.end(), r.channels.begin(), r.channels.end());
                               std::size_t frames = 0;
                               for (const auto& c : result.channels) frames = std::max(frames, c.size());
                               for (auto& c : result.channels) c.resize(frames, 0.0);
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"swap", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               auto a = audio(in);
                               if (a.channels.size() < 2) return fail(tr("pipeline.error.swap_mono"), fmt::arg("n", a.channels.size()));
                               std::swap(a.channels[0], a.channels[1]);
                               return one(std::move(a));
                           }}));
    out.push_back(checked({"pan",
                           kIn,
                           kOut,
                           {{"position", Kind::real, "0", {}, -1, 1}, {"law", Kind::choice, "constant_power", {"constant_power", "linear"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto mono = core::mixdown(audio(in));
                               const double position = param_real(p, "position");
                               const double angle = (position + 1) * std::numbers::pi / 4;
                               const bool linear = param_text(p, "law") == "linear";
                               const double gl = linear ? (1 - position) / 2 : std::cos(angle), gr = linear ? (1 + position) / 2 : std::sin(angle);
                               core::AudioBuffer result;
                               result.sample_rate = mono.sample_rate;
                               const Samples none;
                               const auto& x = mono.channels.empty() ? none : mono.channels.front();
                               for (const double g : {gl, gr}) {
                                   Samples y(x.size());
                                   for (std::size_t i = 0; i < x.size(); ++i) y[i] = g * x[i];
                                   result.channels.push_back(std::move(y));
                               }
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"duplicate", kIn, kOut, {{"channels", Kind::integer, "2", {}, 1, 64}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               core::AudioBuffer result;
                               result.sample_rate = a.sample_rate;
                               if (a.channels.empty()) return one(std::move(result));
                               for (std::size_t ch = 0; ch < static_cast<std::size_t>(param_int(p, "channels")); ++ch) result.channels.push_back(a.channels[ch % a.channels.size()]);
                               return one(std::move(result));
                           }}));
}

}  // namespace signals::pipeline
