#include "core/dtcwt.hpp"
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
    out.push_back(checked({"dtcwt",
                           kIn,
                           kCoeffsOut,
                           {{"levels", Kind::integer, "4", {}, 1, 20},
                            {"biort", Kind::choice, "near_sym_a", {"near_sym_a", "near_sym_b", "antonini", "legall"}},
                            {"qshift", Kind::choice, "qshift_a", {"qshift_a", "qshift_b", "qshift_c", "qshift_d", "qshift_06"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto* biort = core::dtcwt::find_biort(param_text(p, "biort"));
                               const auto* qshift = core::dtcwt::find_qshift(param_text(p, "qshift"));
                               if (!biort || !qshift) return core::make_error(core::Errc::invalid_argument, "dtcwt: unknown filters");
                               const auto levels = static_cast<std::size_t>(param_int(p, "levels"));
                               Coeffs c;
                               c.wavelet = biort->name + "+" + qshift->name;
                               c.rate = a.sample_rate;
                               c.transform = "dtcwt";
                               for (const auto& ch : a.channels) {
                                   if (ch.empty()) return core::make_error(core::Errc::invalid_argument, "dtcwt: empty signal");
                                   Samples x = ch;
                                   if (x.size() % 2 != 0) x.push_back(x.back());
                                   auto pyramid = core::dtcwt::forward(x, levels, *biort, *qshift);
                                   if (!pyramid) return std::unexpected{pyramid.error()};
                                   core::BasicCoeffs<double> bands;
                                   bands.original_length = ch.size();
                                   bands.bands.push_back(std::move(pyramid->lowpass));
                                   for (auto it = pyramid->highpasses.rbegin(); it != pyramid->highpasses.rend(); ++it) {
                                       Samples interleaved;
                                       interleaved.reserve(2 * it->size());
                                       for (const auto& z : *it) interleaved.push_back(z.real()), interleaved.push_back(z.imag());
                                       bands.bands.push_back(std::move(interleaved));
                                   }
                                   c.channels.push_back(std::move(bands));
                                   c.lengths.push_back(ch.size());
                               }
                               return one(std::move(c));
                           }}));
    out.push_back(checked({"idtcwt", kCoeffsIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& c = coeffs(in);
                               if (c.transform != "dtcwt")
                                   return fail(tr("pipeline.error.wrong_inverse"), fmt::arg("transform", c.transform), fmt::arg("inverse", inverse_of(c.transform)));
                               const auto plus = c.wavelet.find('+');
                               const auto* biort = plus == std::string::npos ? nullptr : core::dtcwt::find_biort(std::string_view{c.wavelet}.substr(0, plus));
                               const auto* qshift = plus == std::string::npos ? nullptr : core::dtcwt::find_qshift(std::string_view{c.wavelet}.substr(plus + 1));
                               if (!biort || !qshift) return core::make_error(core::Errc::invalid_argument, "idtcwt: unknown filters " + c.wavelet);
                               core::AudioBuffer result;
                               result.sample_rate = c.rate;
                               for (std::size_t i = 0; i < c.channels.size(); ++i) {
                                   const auto& ch = c.channels[i];
                                   if (ch.bands.empty()) return core::make_error(core::Errc::invalid_argument, "idtcwt: no bands");
                                   core::dtcwt::Pyramid pyramid;
                                   pyramid.lowpass = ch.bands.front();
                                   for (std::size_t b = ch.bands.size(); b-- > 1;) {
                                       const auto& band = ch.bands[b];
                                       if (band.size() % 2 != 0) return core::make_error(core::Errc::invalid_argument, "idtcwt: a band has an odd length");
                                       std::vector<std::complex<double>> z(band.size() / 2);
                                       for (std::size_t k = 0; k < z.size(); ++k) z[k] = {band[2 * k], band[2 * k + 1]};
                                       pyramid.highpasses.push_back(std::move(z));
                                   }
                                   auto y = core::dtcwt::inverse(pyramid, *biort, *qshift);
                                   if (!y) return std::unexpected{y.error()};
                                   if (i < c.lengths.size()) y->resize(c.lengths[i]);
                                   result.channels.push_back(std::move(*y));
                               }
                               return one(std::move(result));
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
