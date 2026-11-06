#include <cmath>
#include <utility>

#include <fmt/format.h>

#include "core/signal_ops.hpp"
#include "i18n/i18n.hpp"
#include "io/generators.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {
namespace {

using Kind = ParamSpec::Kind;
using Out = core::Result<std::vector<Value>>;

const core::AudioBuffer& in_audio(std::span<const Value> in) { return std::get<core::AudioBuffer>(in[0]); }

template <class F>
Out per_channel(std::span<const Value> in, F&& f) {
    core::AudioBuffer out;
    out.sample_rate = in_audio(in).sample_rate;
    for (const auto& c : in_audio(in).channels) out.channels.push_back(f(c));
    return std::vector<Value>{std::move(out)};
}

std::filesystem::path resolve(const RunContext& ctx, const std::string& path) {
    auto p = utf8_path(path);
    return p.is_relative() ? ctx.base_dir / p : p;
}

const std::vector<PortSpec> kOneIn{{"in", PortType::signal}};
const std::vector<PortSpec> kOneOut{{"out", PortType::signal}};

}  // namespace

void register_basic_blocks(std::vector<BlockType>& out) {
    out.push_back({"wav", {}, kOneOut, {{"path", Kind::path, ""}}, [](const RunContext& ctx, std::span<const Value>, const Params& p) -> Out {
                       auto file = io::read_wav(resolve(ctx, param_text(p, "path")), ctx.progress);
                       if (!file) return std::unexpected{file.error()};
                       return std::vector<Value>{std::move(file->audio)};
                   }});
    out.push_back({"gen",
                   {},
                   kOneOut,
                   {{"kind", Kind::choice, "sine", {"sine", "sweep", "impulse", "step", "white", "pink", "harmonics"}},
                    {"freq", Kind::real, "440"},
                    {"f1", Kind::real, "8000"},
                    {"amplitude", Kind::real, "1"},
                    {"rate", Kind::integer, "44100", {}, 1, 768000},
                    {"duration", Kind::real, "1", {}, 0, 3600},
                    {"seed", Kind::integer, "1"}},
                   [](const RunContext& ctx, std::span<const Value>, const Params& p) -> Out {
                       const double rate = static_cast<double>(param_int(p, "rate"));
                       if (param_real(p, "duration") * rate > kMaxSamples)
                           return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.too_long")), fmt::arg("param", "duration"),
                                                                                             fmt::arg("value", param_text(p, "duration"))));
                       const auto n = static_cast<std::size_t>(std::llround(param_real(p, "duration") * rate));
                       const auto& kind = param_text(p, "kind");
                       const double f = param_real(p, "freq"), a = param_real(p, "amplitude");
                       std::vector<double> x;
                       if (kind == "sine") x = io::sine(f, a, 0.0, rate, n, ctx.progress);
                       else if (kind == "sweep") x = io::sweep(f, param_real(p, "f1"), io::SweepKind::logarithmic, rate, n, ctx.progress);
                       else if (kind == "impulse") x = io::impulse(n, 0);
                       else if (kind == "step") x = io::step(n, 0);
                       else if (kind == "white" || kind == "pink")
                           x = io::noise(kind == "white" ? io::NoiseColor::white : io::NoiseColor::pink, a, n, static_cast<std::uint64_t>(param_int(p, "seed")), ctx.progress);
                       else {
                           const std::vector<double> amplitudes{a, a / 2, a / 4, a / 8};
                           x = io::harmonics(f, amplitudes, rate, n, ctx.progress);
                       }
                       if (cancelled(ctx)) return core::make_error(core::Errc::cancelled, "gen: cancelled");
                       core::AudioBuffer buffer;
                       buffer.sample_rate = rate;
                       buffer.channels.push_back(std::move(x));
                       return std::vector<Value>{std::move(buffer)};
                   }});
    out.push_back({"gain", kOneIn, kOneOut, {{"db", Kind::real, "0"}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                       const double db = param_real(p, "db");
                       return per_channel(in, [db](const std::vector<double>& c) { return core::gain_db(c, db); });
                   }});
    out.push_back({"normalize",
                   kOneIn,
                   kOneOut,
                   {{"kind", Kind::choice, "peak", {"peak", "rms"}}, {"target_dbfs", Kind::real, "-1"}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) {
                       const double target = param_real(p, "target_dbfs");
                       const bool peak = param_text(p, "kind") == "peak";
                       return per_channel(in, [&](const std::vector<double>& c) { return peak ? core::normalize_peak(c, target) : core::normalize_rms(c, target); });
                   }});
    out.push_back({"remove_dc", kOneIn, kOneOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                       return per_channel(in, [](const std::vector<double>& c) { return core::remove_dc(c); });
                   }});
    out.push_back({"split",
                   kOneIn,
                   {{"l", PortType::signal}, {"r", PortType::signal, true}},
                   {{"mode", Kind::choice, "lr", {"lr", "channel"}}, {"channel", Kind::integer, "0"}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& a = in_audio(in);
                       if (param_text(p, "mode") == "channel") {
                           auto c = core::select_channel(a, static_cast<std::size_t>(param_int(p, "channel")));
                           if (!c) return std::unexpected{c.error()};
                           return std::vector<Value>{*c, *c};
                       }
                       auto l = core::select_channel(a, 0);
                       if (!l) return std::unexpected{l.error()};
                       auto r = a.channels.size() > 1 ? core::select_channel(a, 1) : l;
                       if (!r) return std::unexpected{r.error()};
                       return std::vector<Value>{std::move(*l), std::move(*r)};
                   }});
    out.push_back({"mixdown", kOneIn, kOneOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                       return std::vector<Value>{core::mixdown(in_audio(in))};
                   }});
    out.push_back({"mid_side",
                   kOneIn,
                   kOneOut,
                   {{"direction", Kind::choice, "encode", {"encode", "decode"}}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       auto r = param_text(p, "direction") == "encode" ? core::to_mid_side(in_audio(in)) : core::from_mid_side(in_audio(in));
                       if (!r) return std::unexpected{r.error()};
                       return std::vector<Value>{std::move(*r)};
                   }});
    out.push_back({"delay", kOneIn, kOneOut, {{"samples", Kind::samples, "0"}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto n = param_samples(p, "samples", in_audio(in).sample_rate);
                       if (!n) return std::unexpected{n.error()};
                       for (const auto& c : in_audio(in).channels)
                           if (auto total = checked_length(c.size() + *n, 1); !total) return std::unexpected{total.error()};
                       return per_channel(in, [d = *n](const std::vector<double>& c) { return core::delay(c, d); });
                   }});
    out.push_back({"reverse", kOneIn, kOneOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                       return per_channel(in, [](const std::vector<double>& c) { return core::reverse(c); });
                   }});
    out.push_back({"cut",
                   kOneIn,
                   kOneOut,
                   {{"begin", Kind::samples, "0"}, {"end", Kind::samples, "0"}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& a = in_audio(in);
                       const auto b = param_samples(p, "begin", a.sample_rate), e = param_samples(p, "end", a.sample_rate);
                       if (!b) return std::unexpected{b.error()};
                       if (!e) return std::unexpected{e.error()};
                       core::AudioBuffer result;
                       result.sample_rate = a.sample_rate;
                       for (const auto& c : a.channels) {
                           auto piece = core::cut(c, *b, *e);
                           if (!piece) return std::unexpected{piece.error()};
                           result.channels.push_back(std::move(*piece));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
}

}  // namespace signals::pipeline
