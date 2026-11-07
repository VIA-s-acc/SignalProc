#include <algorithm>

#include <fmt/format.h>

#include "core/adaptive.hpp"
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
}

}  // namespace signals::pipeline
