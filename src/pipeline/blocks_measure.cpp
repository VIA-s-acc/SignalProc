#include <cmath>
#include <map>

#include "core/spectral.hpp"
#include "core/expr.hpp"
#include "pipeline/blocks_common.hpp"
#include "pipeline/expr_text.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

constexpr double kFloorDb = -200;
constexpr std::size_t kSegment = 4096;

double db_of_power(double power) { return power > 0 ? std::max(kFloorDb, 10 * std::log10(power)) : kFloorDb; }

struct Spectrum {
    double centroid = 0, dominant = 0;
};

Spectrum spectrum_of(const Samples& x, double rate) {
    const auto segment = std::min(x.size(), kSegment);
    if (segment < 2) return {};
    const auto psd = core::welch_psd(x, rate, segment, segment / 2);
    if (!psd) return {};
    const auto f = core::welch_frequencies(rate, segment);
    double total = 0, moment = 0;
    for (std::size_t k = 0; k < psd->size(); ++k) total += (*psd)[k], moment += f[k] * (*psd)[k];
    if (!(total > 0)) return {};
    const auto& p = *psd;
    const auto k = static_cast<std::size_t>(std::ranges::max_element(p) - p.begin());
    double offset = 0;
    if (k > 0 && k + 1 < p.size() && p[k - 1] > 0 && p[k + 1] > 0) {
        const double a = std::log(p[k - 1]), b = std::log(p[k]), c = std::log(p[k + 1]);
        if (const double curve = a - 2 * b + c; curve < 0) offset = 0.5 * (a - c) / curve;
    }
    return {moment / total, (static_cast<double>(k) + offset) * rate / static_cast<double>(segment)};
}

void channel_stats(Report& r, const Samples& x, double rate, const std::string& suffix) {
    const auto n = static_cast<double>(x.size());
    double sum = 0, squares = 0, peak = 0;
    std::size_t crossings = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sum += x[i];
        squares += x[i] * x[i];
        peak = std::max(peak, std::abs(x[i]));
        if (i && (x[i - 1] >= 0) != (x[i] >= 0)) ++crossings;
    }
    const double mean = x.empty() ? 0 : sum / n, rms = x.empty() ? 0 : std::sqrt(squares / n);
    double spread = 0;
    for (const double v : x) spread += (v - mean) * (v - mean);
    const auto s = spectrum_of(x, rate);
    r.fields.push_back({"rms" + suffix, rms});
    r.fields.push_back({"peak" + suffix, peak});
    r.fields.push_back({"crest_db" + suffix, rms > 0 ? 20 * std::log10(peak / rms) : 0.0});
    r.fields.push_back({"mean" + suffix, mean});
    r.fields.push_back({"std" + suffix, x.empty() ? 0 : std::sqrt(spread / n)});
    r.fields.push_back({"zcr_per_s" + suffix, x.empty() ? 0 : static_cast<double>(crossings) * rate / n});
    r.fields.push_back({"centroid_hz" + suffix, s.centroid});
    r.fields.push_back({"dominant_hz" + suffix, s.dominant});
}

}  // namespace

void register_measure_blocks(std::vector<BlockType>& out) {
    out.push_back({"assert",
                   {{"a", PortType::report}, {"b", PortType::report, true}, {"c", PortType::report, true}, {"d", PortType::report, true}},
                   kReportOut,
                   {{"expr", Kind::text, "a.passed"}},
                   [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                       const auto& text = param_text(p, "expr");
                       auto e = core::parse_expr(text);
                       if (!e) return fail(tr("pipeline.error.assert_expr"), fmt::arg("why", describe_expr_error(e.error())));
                       std::map<std::string, double, std::less<>> names;
                       static constexpr std::string_view kPorts[] = {"a", "b", "c", "d"};
                       for (std::size_t k = 0; k < in.size() && k < 4; ++k)
                           if (const auto* r = std::get_if<Report>(&in[k])) {
                               const auto port = std::string{kPorts[k]};
                               names[port + ".passed"] = r->passed ? 1.0 : 0.0;
                               for (const auto& [field, value] : r->fields)
                                   if (const auto* d = std::get_if<double>(&value)) names[port + "." + field] = *d;
                           }
                       auto v = core::eval(*e, [&](std::string_view name) -> std::optional<double> {
                           const auto it = names.find(name);
                           if (it != names.end()) return it->second;
                           return std::nullopt;
                       });
                       if (!v) {
                           if (v.error().kind == core::ExprError::Kind::unknown_name) {
                               std::vector<std::string> known;
                               for (const auto& [name, value] : names) known.push_back(name);
                               const auto near = nearest_name(v.error().token, known);
                               return fail(tr("pipeline.error.assert_name"), fmt::arg("name", v.error().token), fmt::arg("near", near.value_or("—")));
                           }
                           return fail(tr("pipeline.error.assert_expr"), fmt::arg("why", describe_expr_error(v.error())));
                       }
                       Report r;
                       r.fields = {{"value", *v}};
                       r.passed = *v != 0;
                       return std::vector<Value>{std::move(r)};
                   }});
    out.push_back(checked({"stats", kIn, kReportOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& a = audio(in);
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c)
                                   channel_stats(r, a.channels[c], a.sample_rate, a.channels.size() > 1 ? "_ch" + std::to_string(c) : std::string{});
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"loudness",
                           kIn,
                           kReportOut,
                           {{"window", Kind::samples, "0.4s"}, {"hop", Kind::samples, "0.1s"}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto window = param_samples(p, "window", a.sample_rate), hop = param_samples(p, "hop", a.sample_rate);
                               if (!window) return std::unexpected{window.error()};
                               if (!hop) return std::unexpected{hop.error()};
                               if (*window == 0 || *hop == 0) return fail(tr("pipeline.error.loudness_window"));
                               const auto frames = a.frames();
                               const auto length = std::min(*window, frames);
                               std::size_t count = 0;
                               double loudest = 0, total = 0;
                               for (std::size_t start = 0; frames > 0 && start + length <= frames; start += *hop, ++count) {
                                   double power = 0;
                                   for (const auto& c : a.channels)
                                       for (std::size_t i = start; i < std::min(start + length, c.size()); ++i) power += c[i] * c[i];
                                   power /= static_cast<double>(length * a.channels.size());
                                   loudest = std::max(loudest, power);
                                   total += power;
                               }
                               Report r;
                               r.fields = {{"windows", static_cast<double>(count)},
                                           {"max_dbfs", db_of_power(loudest)},
                                           {"mean_dbfs", db_of_power(count ? total / static_cast<double>(count) : 0)}};
                               return one(std::move(r));
                           }}));
}

}  // namespace signals::pipeline
