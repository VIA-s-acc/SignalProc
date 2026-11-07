#include <algorithm>
#include <cmath>
#include <optional>

#include <fmt/format.h>

#include "core/adaptive.hpp"
#include "core/restoration.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {
namespace {

using Kind = ParamSpec::Kind;
using Out = core::Result<std::vector<Value>>;

const core::AudioBuffer& audio_in(std::span<const Value> in, std::size_t i) { return std::get<core::AudioBuffer>(in[i]); }

core::Result<void> same_rate(const char* block, const core::AudioBuffer& a, const core::AudioBuffer& b) {
    if (b.channels.empty()) return core::make_error(core::Errc::invalid_argument, std::string{block} + ": the second input has no channels");
    if (a.sample_rate != b.sample_rate)
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.rates")), fmt::arg("block", block),
                                                                          fmt::arg("a", a.sample_rate), fmt::arg("b", b.sample_rate)));
    return {};
}

}  // namespace

void register_restore_blocks(std::vector<BlockType>& out) {
    out.push_back({"lms",
                   {{"x", PortType::signal}, {"d", PortType::signal}},
                   {{"e", PortType::signal}, {"y", PortType::signal}},
                   {{"taps", Kind::integer, "32", {}, 1, 4096}, {"mu", Kind::real, "0.1", {}, 1e-6, 2}, {"rule", Kind::choice, "nlms", {"nlms", "lms"}}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& x = audio_in(in, 0);
                       const auto& d = audio_in(in, 1);
                       if (auto ok = same_rate("lms", d, x); !ok) return std::unexpected{ok.error()};
                       const auto rule = param_text(p, "rule") == "lms" ? core::AdaptiveRule::lms : core::AdaptiveRule::nlms;
                       core::AudioBuffer e, y;
                       e.sample_rate = y.sample_rate = d.sample_rate;
                       for (std::size_t ch = 0; ch < d.channels.size(); ++ch) {
                           auto r = core::adaptive_filter(x.channels[std::min(ch, x.channels.size() - 1)], d.channels[ch], static_cast<std::size_t>(param_int(p, "taps")),
                                                          param_real(p, "mu"), rule);
                           if (!r) return std::unexpected{r.error()};
                           e.channels.push_back(std::move(r->e));
                           y.channels.push_back(std::move(r->y));
                       }
                       return std::vector<Value>{std::move(e), std::move(y)};
                   }});
    out.push_back({"wiener",
                   {{"in", PortType::signal}},
                   {{"out", PortType::signal}},
                   {{"size", Kind::integer, "5", {}, 1, 1001}, {"noise", Kind::real, "0", {}, 0, 1e6}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& a = audio_in(in, 0);
                       const double level = param_real(p, "noise");
                       core::AudioBuffer result;
                       result.sample_rate = a.sample_rate;
                       for (const auto& ch : a.channels) {
                           auto r = core::wiener_local(ch, static_cast<std::size_t>(param_int(p, "size")), level > 0 ? std::optional<double>{level} : std::nullopt);
                           if (!r) return std::unexpected{r.error()};
                           result.channels.push_back(std::move(*r));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
    out.push_back({"spectral_subtract",
                   {{"x", PortType::signal}, {"noise", PortType::signal, true}},
                   {{"out", PortType::signal}},
                   {{"alpha", Kind::real, "2", {}, 0, 10},
                    {"beta", Kind::real, "0.01", {}, 0, 1},
                    {"n", Kind::integer, "512", {}, 16, 16384},
                    {"noise_s", Kind::real, "0.25", {}, 0.001, 60}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& x = audio_in(in, 0);
                       const auto& noise = audio_in(in, 1);
                       if (!noise.channels.empty())
                           if (auto ok = same_rate("spectral_subtract", x, noise); !ok) return std::unexpected{ok.error()};
                       core::SpectralSubtraction o;
                       o.alpha = param_real(p, "alpha");
                       o.beta = param_real(p, "beta");
                       o.n = static_cast<std::size_t>(param_int(p, "n"));
                       o.hop = std::max<std::size_t>(1, o.n / 4);
                       const auto lead = static_cast<std::size_t>(std::llround(param_real(p, "noise_s") * x.sample_rate));
                       core::AudioBuffer result;
                       result.sample_rate = x.sample_rate;
                       for (std::size_t ch = 0; ch < x.channels.size(); ++ch) {
                           const auto& signal = x.channels[ch];
                           std::span<const double> segment;
                           if (!noise.channels.empty()) {
                               segment = noise.channels[std::min(ch, noise.channels.size() - 1)];
                           } else {
                               if (lead > signal.size())
                                   return core::make_error(core::Errc::invalid_argument, "spectral_subtract: noise_s is longer than the signal; connect a noise input or shorten noise_s");
                               segment = std::span<const double>{signal}.first(lead);
                           }
                           auto r = core::spectral_subtract(signal, segment, o);
                           if (!r) return std::unexpected{r.error()};
                           result.channels.push_back(std::move(*r));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
    out.push_back({"deconvolve",
                   {{"y", PortType::signal}, {"h", PortType::signal}},
                   {{"out", PortType::signal}},
                   {{"snr_db", Kind::real, "30", {}, -20, 120}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& y = audio_in(in, 0);
                       const auto& h = audio_in(in, 1);
                       if (auto ok = same_rate("deconvolve", y, h); !ok) return std::unexpected{ok.error()};
                       core::AudioBuffer result;
                       result.sample_rate = y.sample_rate;
                       for (std::size_t ch = 0; ch < y.channels.size(); ++ch) {
                           auto r = core::wiener_deconvolve(y.channels[ch], h.channels[std::min(ch, h.channels.size() - 1)], param_real(p, "snr_db"));
                           if (!r) return std::unexpected{r.error()};
                           result.channels.push_back(std::move(*r));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
}

}  // namespace signals::pipeline
