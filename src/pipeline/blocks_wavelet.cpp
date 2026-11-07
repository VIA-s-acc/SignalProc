#include "core/lifting.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

core::BasicCoeffs<double> lift97_levels(const Samples& x, std::size_t levels) {
    core::BasicCoeffs<double> out;
    out.original_length = x.size();
    Samples approx = x;
    std::vector<Samples> details;
    for (std::size_t j = 0; j < levels; ++j) {
        auto l = core::lift97(approx);
        details.push_back(std::move(l.d));
        approx = std::move(l.s);
    }
    out.bands.push_back(std::move(approx));
    for (auto it = details.rbegin(); it != details.rend(); ++it) out.bands.push_back(std::move(*it));
    return out;
}

}  // namespace

void register_wavelet_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"lift97", kIn, kCoeffsOut, {{"levels", Kind::integer, "4", {}, 1, 30}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto levels = static_cast<std::size_t>(param_int(p, "levels"));
                               Coeffs c;
                               c.wavelet = "lift97";
                               c.transform = "lift97";
                               c.mode = core::Extension::symmetric;
                               c.rate = a.sample_rate;
                               for (const auto& ch : a.channels) {
                                   if (ch.size() < (std::size_t{1} << levels))
                                       return fail(tr("pipeline.error.lift97_levels"), fmt::arg("levels", tr_count("unit.level", static_cast<long long>(levels))),
                                                   fmt::arg("n", tr_count("unit.sample", static_cast<long long>(ch.size()))));
                                   c.channels.push_back(lift97_levels(ch, levels));
                                   c.lengths.push_back(ch.size());
                               }
                               return one(std::move(c));
                           }}));
    out.push_back(checked({"ilift97", kCoeffsIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& c = coeffs(in);
                               if (c.transform != "lift97")
                                   return fail(tr("pipeline.error.wrong_inverse"), fmt::arg("transform", c.transform), fmt::arg("inverse", inverse_of(c.transform)));
                               core::AudioBuffer result;
                               result.sample_rate = c.rate;
                               for (const auto& ch : c.channels) {
                                   Samples y = ch.bands.front();
                                   for (std::size_t b = 1; b < ch.bands.size(); ++b) y = core::unlift97({std::move(y), ch.bands[b]});
                                   result.channels.push_back(std::move(y));
                               }
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"mra", kCoeffsIn, kOut, {{"component", Kind::text, "d1"}}, [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto& c = coeffs(in);
                               const auto& name = param_text(p, "component");
                               const auto keep = band_index(c, name);
                               if (!keep) return fail(tr("pipeline.error.mra_component"), fmt::arg("component", name), fmt::arg("levels", c.levels()));
                               Coeffs only = c;
                               for (auto& ch : only.channels)
                                   for (std::size_t b = 0; b < ch.bands.size(); ++b)
                                       if (b != *keep) std::ranges::fill(ch.bands[b], 0.0);
                               const auto* inverse = find_block_type(inverse_of(c.transform));
                               const auto params = resolve_params(*inverse, {});
                               if (!params) return std::unexpected{params.error()};
                               std::vector<Value> args{std::move(only)};
                               const RunContext inner{ctx.base_dir, ctx.progress, {true}};
                               return inverse->run(inner, args, *params);
                           }}));
}

}  // namespace signals::pipeline
