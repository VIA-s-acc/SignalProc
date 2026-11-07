#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>

#include <fmt/format.h>

#include "core/check.hpp"
#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/multirate.hpp"
#include "core/process/shrink.hpp"
#include "core/process/threshold.hpp"
#include "core/swt.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {
namespace {

using Kind = ParamSpec::Kind;
using Out = core::Result<std::vector<Value>>;

const std::vector<PortSpec> kSignalIn{{"in", PortType::signal}};
const std::vector<PortSpec> kSignalOut{{"out", PortType::signal}};
const std::vector<PortSpec> kCoeffsIn{{"in", PortType::coeffs}};
const std::vector<PortSpec> kCoeffsOut{{"coeffs", PortType::coeffs}};
const std::vector<PortSpec> kReportOut{{"report", PortType::report}};
constexpr double kMaxFactor = 1024;
const ParamSpec kAttenuation{"attenuation_db", Kind::real, "80", {}, 10, 200};
const std::vector<std::string> kExtensions{"symmetric", "periodization", "zero", "constant", "smooth", "periodic", "reflect", "antisymmetric", "antireflect"};
const std::vector<std::string> kExtensionsOrAuto{"auto", "symmetric", "periodization", "zero", "constant", "smooth", "periodic", "reflect", "antisymmetric", "antireflect"};

const core::AudioBuffer& in_audio(std::span<const Value> in, std::size_t i = 0) { return std::get<core::AudioBuffer>(in[i]); }
const Coeffs& in_coeffs(std::span<const Value> in) { return std::get<Coeffs>(in[0]); }

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in{s};
    for (std::string item; std::getline(in, item, ',');) {
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

core::Result<core::FilterBank> bank_of(const std::string& name) { return core::wavelet(name); }

core::Result<core::Laurent> filter_of(const std::string& spec) {
    const auto dot = spec.rfind('.');
    if (dot == std::string::npos) return core::make_error(core::Errc::invalid_argument, "filter must look like db2.h0");
    auto bank = bank_of(spec.substr(0, dot));
    if (!bank) return std::unexpected{bank.error()};
    const auto role = spec.substr(dot + 1);
    if (role == "h0") return bank->h0;
    if (role == "h1") return bank->h1;
    if (role == "f0") return bank->f0;
    if (role == "f1") return bank->f1;
    return core::make_error(core::Errc::invalid_argument, "filter role must be h0, h1, f0 or f1");
}

template <class F>
Out per_channel(const core::AudioBuffer& a, double rate, F&& f) {
    core::AudioBuffer out;
    out.sample_rate = rate;
    for (const auto& c : a.channels) {
        auto r = f(c);
        if (!r) return std::unexpected{r.error()};
        out.channels.push_back(std::move(*r));
    }
    return std::vector<Value>{std::move(out)};
}

core::MultirateOptions multirate(const Params& p) {
    core::MultirateOptions o;
    o.attenuation_db = param_real(p, "attenuation_db");
    if (const auto it = p.find("anti_alias"); it != p.end()) o.anti_alias = it->second == "yes";
    return o;
}

}  // namespace

void register_dsp_blocks(std::vector<BlockType>& out) {
    out.push_back({"downsample",
                   kSignalIn,
                   kSignalOut,
                   {{"m", Kind::integer, "2", {}, 1, kMaxFactor}, kAttenuation, {"anti_alias", Kind::choice, "yes", {"yes", "no"}}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) {
                       const auto m = static_cast<std::size_t>(param_int(p, "m"));
                       const auto o = multirate(p);
                       return per_channel(in_audio(in), in_audio(in).sample_rate / static_cast<double>(m),
                                          [&](const std::vector<double>& c) { return core::decimate(c, m, o); });
                   }});
    out.push_back({"upsample",
                   kSignalIn,
                   kSignalOut,
                   {{"m", Kind::integer, "2", {}, 1, kMaxFactor}, kAttenuation},
                   [](const RunContext&, std::span<const Value> in, const Params& p) {
                       const auto m = static_cast<std::size_t>(param_int(p, "m"));
                       const auto o = multirate(p);
                       return per_channel(in_audio(in), in_audio(in).sample_rate * static_cast<double>(m),
                                          [&](const std::vector<double>& c) -> core::Result<std::vector<double>> {
                                              if (auto n = checked_length(c.size(), m); !n) return std::unexpected{n.error()};
                                              return core::interpolate(c, m, o);
                                          });
                   }});
    out.push_back({"resample",
                   kSignalIn,
                   kSignalOut,
                   {{"l", Kind::integer, "1", {}, 1, kMaxFactor}, {"m", Kind::integer, "1", {}, 1, kMaxFactor}, kAttenuation},
                   [](const RunContext&, std::span<const Value> in, const Params& p) {
                       const auto l = static_cast<std::size_t>(param_int(p, "l"));
                       const auto m = static_cast<std::size_t>(param_int(p, "m"));
                       const auto o = multirate(p);
                       return per_channel(in_audio(in), in_audio(in).sample_rate * static_cast<double>(l) / static_cast<double>(m),
                                          [&](const std::vector<double>& c) -> core::Result<std::vector<double>> {
                                              if (auto n = checked_length(c.size(), l); !n) return std::unexpected{n.error()};
                                              auto up = l > 1 ? core::interpolate(c, l, o) : core::Result<std::vector<double>>{c};
                                              if (!up || m == 1) return up;
                                              return core::decimate(*up, m, o);
                                          });
                   }});
    out.push_back({"convolve",
                   kSignalIn,
                   kSignalOut,
                   {{"filter", Kind::text, "db2.h0"}, {"method", Kind::choice, "auto", {"auto", "direct", "fft"}}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       auto h = filter_of(param_text(p, "filter"));
                       if (!h) return std::unexpected{h.error()};
                       const auto& m = param_text(p, "method");
                       const auto method = m == "direct" ? core::ConvMethod::direct : m == "fft" ? core::ConvMethod::fft : core::ConvMethod::automatic;
                       return per_channel(in_audio(in), in_audio(in).sample_rate, [&](const std::vector<double>& c) -> core::Result<std::vector<double>> {
                           return core::convolve(core::Laurent{c, 0}, *h, method).coeffs;
                       });
                   }});
    out.push_back({"dwt",
                   kSignalIn,
                   kCoeffsOut,
                   {{"wavelet", Kind::text, "db2"}, {"levels", Kind::integer, "0", {}, 0, 64}, {"ext", Kind::choice, "symmetric", kExtensions}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       auto bank = bank_of(param_text(p, "wavelet"));
                       if (!bank) return std::unexpected{bank.error()};
                       const auto& a = in_audio(in);
                       const auto mode = core::parse_extension(param_text(p, "ext"));
                       Coeffs c;
                       c.wavelet = param_text(p, "wavelet");
                       c.mode = *mode;
                       c.rate = a.sample_rate;
                       const double share = 1.0 / static_cast<double>(std::max<std::size_t>(1, a.channels.size()));
                       for (const auto& ch : a.channels) {
                           const auto levels = param_int(p, "levels") > 0 ? static_cast<std::size_t>(param_int(p, "levels")) : core::default_levels(ch.size(), bank->length());
                           ScaledProgress part{ctx.progress, share * static_cast<double>(c.channels.size()), share};
                           auto r = core::wavedec<double>(ch, *bank, levels, {*mode, false}, &part);
                           if (!r) return std::unexpected{r.error()};
                           c.channels.push_back(std::move(*r));
                           c.lengths.push_back(ch.size());
                       }
                       return std::vector<Value>{std::move(c)};
                   }});
    out.push_back({"idwt",
                   kCoeffsIn,
                   kSignalOut,
                   {{"wavelet", Kind::text, "auto"}, {"ext", Kind::choice, "auto", kExtensionsOrAuto}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       const auto& c = in_coeffs(in);
                       if (c.transform != "dwt") return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.wrong_inverse")), fmt::arg("transform", c.transform), fmt::arg("inverse", "iswt")));
                       const auto& wavelet = param_text(p, "wavelet");
                       const auto& ext = param_text(p, "ext");
                       if (wavelet != "auto" && wavelet != c.wavelet)
                           return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.idwt_wavelet")),
                                                                                             fmt::arg("coeffs", c.wavelet), fmt::arg("wavelet", wavelet)));
                       const auto mode = ext == "auto" ? std::optional{c.mode} : core::parse_extension(ext);
                       if (*mode != c.mode)
                           return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.idwt_ext")),
                                                                                             fmt::arg("coeffs", std::string{core::to_string(c.mode)}), fmt::arg("ext", ext)));
                       auto bank = bank_of(c.wavelet);
                       if (!bank) return std::unexpected{bank.error()};
                       core::AudioBuffer result;
                       result.sample_rate = c.rate;
                       const double share = 1.0 / static_cast<double>(std::max<std::size_t>(1, c.channels.size()));
                       for (std::size_t i = 0; i < c.channels.size(); ++i) {
                           ScaledProgress part{ctx.progress, share * static_cast<double>(i), share};
                           auto y = core::waverec<double>(c.channels[i], *bank, {*mode, false}, &part);
                           if (!y) return std::unexpected{y.error()};
                           if (i < c.lengths.size()) y->resize(c.lengths[i]);
                           result.channels.push_back(std::move(*y));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
    out.push_back({"swt",
                   kSignalIn,
                   kCoeffsOut,
                   {{"wavelet", Kind::text, "db2"}, {"levels", Kind::integer, "0", {}, 0, 32}, {"norm", Kind::choice, "no", {"no", "yes"}}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       auto bank = bank_of(param_text(p, "wavelet"));
                       if (!bank) return std::unexpected{bank.error()};
                       const auto& a = in_audio(in);
                       Coeffs c;
                       c.wavelet = param_text(p, "wavelet");
                       c.mode = core::Extension::periodization;
                       c.rate = a.sample_rate;
                       c.transform = "swt";
                       c.norm = param_text(p, "norm") == "yes";
                       const double share = 1.0 / static_cast<double>(std::max<std::size_t>(1, a.channels.size()));
                       for (const auto& ch : a.channels) {
                           const auto levels = param_int(p, "levels") > 0 ? static_cast<std::size_t>(param_int(p, "levels")) : core::default_levels(ch.size(), bank->length());
                           if (ch.empty()) return core::make_error(core::Errc::invalid_argument, "swt: empty signal");
                           if (levels >= 63 || (std::size_t{1} << levels) > ch.size())
                               return core::make_error(core::Errc::level_out_of_range, fmt::format(fmt::runtime(tr("pipeline.error.swt_levels")), fmt::arg("levels", levels), fmt::arg("n", ch.size())));
                           const std::size_t block = std::size_t{1} << levels;
                           const auto size = (ch.size() + block - 1) / block * block;
                           if (auto ok = checked_length(size, levels + 1); !ok) return std::unexpected{ok.error()};
                           std::vector<double> padded(size);
                           for (std::size_t i = 0; i < size; ++i) padded[i] = ch[i % ch.size()];
                           ScaledProgress part{ctx.progress, share * static_cast<double>(c.channels.size()), share};
                           auto r = core::swt<double>(padded, *bank, levels, {c.norm, false}, &part);
                           if (!r) return std::unexpected{r.error()};
                           c.channels.push_back(std::move(*r));
                           c.lengths.push_back(ch.size());
                       }
                       return std::vector<Value>{std::move(c)};
                   }});
    out.push_back({"iswt",
                   kCoeffsIn,
                   kSignalOut,
                   {{"wavelet", Kind::text, "auto"}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       const auto& c = in_coeffs(in);
                       if (c.transform != "swt") return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.wrong_inverse")), fmt::arg("transform", c.transform), fmt::arg("inverse", "idwt")));
                       const auto& wavelet = param_text(p, "wavelet");
                       if (wavelet != "auto" && wavelet != c.wavelet)
                           return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.idwt_wavelet")), fmt::arg("coeffs", c.wavelet), fmt::arg("wavelet", wavelet)));
                       auto bank = bank_of(c.wavelet);
                       if (!bank) return std::unexpected{bank.error()};
                       core::AudioBuffer result;
                       result.sample_rate = c.rate;
                       const double share = 1.0 / static_cast<double>(std::max<std::size_t>(1, c.channels.size()));
                       for (std::size_t i = 0; i < c.channels.size(); ++i) {
                           ScaledProgress part{ctx.progress, share * static_cast<double>(i), share};
                           auto y = core::iswt<double>(c.channels[i], *bank, {c.norm, false}, &part);
                           if (!y) return std::unexpected{y.error()};
                           if (i < c.lengths.size()) y->resize(c.lengths[i]);
                           result.channels.push_back(std::move(*y));
                       }
                       return std::vector<Value>{std::move(result)};
                   }});
    out.push_back({"threshold",
                   kCoeffsIn,
                   kCoeffsOut,
                   {{"rule", Kind::choice, "bayes", {"bayes", "visu", "sure", "minimax"}},
                    {"mode", Kind::choice, "soft", {"soft", "hard", "garrote"}},
                    {"levels", Kind::range, "1..64"},
                    {"sigma", Kind::sigma, "mad_d1"}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       Coeffs c = take<Coeffs>(ctx, in, 0);
                       const auto [first, last] = param_range(p, "levels");
                       const auto mode = *core::parse_threshold_mode(param_text(p, "mode"));
                       const auto rule = core::parse_shrink_rule(param_text(p, "rule")).value_or(core::ShrinkRule::bayes);
                       for (std::size_t ch = 0; ch < c.channels.size(); ++ch) {
                           auto& bands = c.channels[ch].bands;
                           const auto J = bands.size() - 1;
                           double sigma = 0;
                           if (param_text(p, "sigma") == "mad_d1") sigma = core::noise_sigma_mad(bands.back());
                           else sigma = param_real(p, "sigma") * (c.transform == "swt" && c.norm ? std::sqrt(0.5) : 1.0);
                           if (sigma == 0) continue;
                           const auto n = ch < c.lengths.size() ? c.lengths[ch] : 0;
                           for (std::size_t b = 1; b <= J; ++b) {
                               const auto level = J + 1 - b;
                               if (level < first || level > last) continue;
                               const double level_sigma = sigma * core::swt_noise_scale(level, c.transform == "swt" && c.norm);
                               const double t = core::rule_threshold(rule, bands[b], level_sigma, n);
                               bands[b] = core::threshold(bands[b], t, mode);
                           }
                       }
                       return std::vector<Value>{std::move(c)};
                   }});
    out.push_back({"band_edit",
                   kCoeffsIn,
                   kCoeffsOut,
                   {{"enabled", Kind::text, "all"}, {"gains_db", Kind::gains, ""}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       Coeffs c = take<Coeffs>(ctx, in, 0);
                       const auto enabled = split_list(param_text(p, "enabled"));
                       std::vector<bool> keep(c.levels() + 1, enabled.empty() || enabled.front() == "all");
                       for (const auto& name : enabled) {
                           if (name == "all") continue;
                           const auto i = band_index(c, name);
                           if (!i) return core::make_error(core::Errc::invalid_argument, "band_edit: unknown band " + name);
                           keep[*i] = true;
                       }
                       std::vector<double> gains(c.levels() + 1, 1.0);
                       for (const auto& item : split_list(param_text(p, "gains_db"))) {
                           const auto eq = item.find('=');
                           const auto i = eq == std::string::npos ? std::nullopt : band_index(c, item.substr(0, eq));
                           if (!i) return core::make_error(core::Errc::invalid_argument, "band_edit: gains look like d2=-6");
                           Params one{{"g", item.substr(eq + 1)}};
                           gains[*i] = std::pow(10.0, param_real(one, "g") / 20);
                       }
                       for (auto& ch : c.channels)
                           for (std::size_t b = 0; b < ch.bands.size(); ++b)
                               for (double& v : ch.bands[b]) v = keep[b] ? v * gains[b] : 0.0;
                       return std::vector<Value>{std::move(c)};
                   }});
    out.push_back({"band_energy", kCoeffsIn, kReportOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                       const auto& c = in_coeffs(in);
                       Report r;
                       for (std::size_t ch = 0; ch < c.channels.size(); ++ch) {
                           const auto J = c.channels[ch].bands.size() - 1;
                           const auto weights = c.transform == "swt" ? core::swt_energy_weights(J, c.norm) : std::vector<double>{};
                           const auto fractions = core::band_energy_fractions(c.channels[ch], weights);
                           for (std::size_t b = 0; b < fractions.size(); ++b)
                               r.fields.emplace_back("c" + std::to_string(ch) + "." + (b == 0 ? "a" + std::to_string(J) : "d" + std::to_string(J + 1 - b)), fractions[b]);
                       }
                       return std::vector<Value>{std::move(r)};
                   }});
    out.push_back({"check",
                   {{"ref", PortType::signal}, {"in", PortType::signal}},
                   kReportOut,
                   {{"kind", Kind::choice, "pr", {"pr", "error", "snr"}}, {"tol", Kind::real, "1e-12"}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& ref = in_audio(in, 0);
                       const auto& test = in_audio(in, 1);
                       Report r;
                       double worst = 0, rms = 0, snr = std::numeric_limits<double>::infinity();
                       for (std::size_t ch = 0; ch < std::min(ref.channels.size(), test.channels.size()); ++ch) {
                           const auto n = std::min(ref.channels[ch].size(), test.channels[ch].size());
                           const std::span<const double> a{ref.channels[ch].data(), n}, b{test.channels[ch].data(), n};
                           worst = std::max(worst, core::max_abs_error(a, b));
                           rms = std::max(rms, core::rms_error(a, b));
                           snr = std::min(snr, core::snr_db(a, b));
                       }
                       const auto& kind = param_text(p, "kind");
                       const double tol = param_real(p, "tol");
                       r.fields = {{"max_abs", worst}, {"rms", rms}, {"snr_db", snr}};
                       r.passed = kind == "snr" ? snr >= tol : worst <= tol;
                       if (ref.channels.size() != test.channels.size()) r.passed = false;
                       return std::vector<Value>{std::move(r)};
                   }});
    out.push_back({"wav_out",
                   kSignalIn,
                   kReportOut,
                   {{"path", Kind::path, "out.wav"},
                    {"bits", Kind::choice, "24", {"16", "24", "32", "f32"}},
                    {"dither", Kind::choice, "tpdf", {"none", "tpdf"}},
                    {"seed", Kind::integer, "24301"}},
                   [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                       auto path = utf8_path(param_text(p, "path"));
                       if (path.is_relative()) path = ctx.base_dir / path;
                       const auto& bits = param_text(p, "bits");
                       io::WavWriteOptions o;
                       o.format = bits == "16" ? io::SampleFormat::pcm_s16 : bits == "32" ? io::SampleFormat::pcm_s32 : bits == "f32" ? io::SampleFormat::float32 : io::SampleFormat::pcm_s24;
                       o.dither = param_text(p, "dither") == "tpdf";
                       o.dither_seed = static_cast<std::uint64_t>(param_int(p, "seed"));
                       const auto written = io::write_wav(path, in_audio(in), o, ctx.progress);
                       if (!written) return std::unexpected{written.error()};
                       Report r;
                       r.fields = {{"path", path.string()}, {"clipped", static_cast<double>(written->clipped_samples)}};
                       return std::vector<Value>{std::move(r)};
                   },
                   true});
    out.push_back({"listen", kSignalIn, {}, {}, [](const RunContext&, std::span<const Value>, const Params&) -> Out { return std::vector<Value>{}; }, true});
}

}  // namespace signals::pipeline
