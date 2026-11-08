#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>

#include "core/analysis.hpp"
#include "core/metrics.hpp"
#include "core/pitch.hpp"
#include "core/spectral.hpp"
#include "core/swt.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

constexpr double kPowerFloor = 1e-10;

std::string suffix(const core::AudioBuffer& a, std::size_t c) { return a.channels.size() > 1 ? "_ch" + std::to_string(c) : std::string{}; }

std::optional<core::Error> finite_input(std::string_view block, const core::AudioBuffer& a) {
    for (const auto& c : a.channels)
        if (!std::ranges::all_of(c, [](double v) { return std::isfinite(v); }))
            return core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.input_not_finite")), fmt::arg("block", block))};
    return std::nullopt;
}

std::optional<core::Error> needs_rate(std::string_view block, const core::AudioBuffer& a) {
    if (a.sample_rate > 0) return std::nullopt;
    return core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.needs_rate")), fmt::arg("block", block))};
}

std::optional<core::Error> pair_checks(std::string_view block, const core::AudioBuffer& a, const core::AudioBuffer& b) {
    if (auto e = same_rate(block, a, b)) return e;
    if (auto e = finite_input(block, a)) return e;
    if (auto e = finite_input(block, b)) return e;
    const bool empty = std::ranges::any_of(a.channels, [](const Samples& c) { return c.empty(); }) ||
                       std::ranges::any_of(b.channels, [](const Samples& c) { return c.empty(); }) || (!a.channels.empty() && b.channels.empty());
    if (empty) return core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.pair_empty")), fmt::arg("block", block))};
    return std::nullopt;
}

double mean(const std::vector<double>& v) { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()); }

std::vector<std::vector<double>> mel_bank(double rate, std::size_t n, std::size_t mels) {
    const std::size_t bins = n / 2 + 1;
    const double top = 2595 * std::log10(1 + rate / 2 / 700), step = top / static_cast<double>(mels + 1);
    std::vector<double> edges(mels + 2);
    for (std::size_t k = 0; k < edges.size(); ++k) {
        const double m = k + 1 == edges.size() ? top : static_cast<double>(k) * step;
        edges[k] = 700 * (std::pow(10.0, m / 2595) - 1);
    }
    std::vector<std::vector<double>> bank(mels, std::vector<double>(bins, 0.0));
    for (std::size_t m = 0; m < mels; ++m)
        for (std::size_t b = 0; b < bins; ++b) {
            const double f = static_cast<double>(b) * rate / static_cast<double>(n);
            const double up = (f - edges[m]) / (edges[m + 1] - edges[m]), down = (edges[m + 2] - f) / (edges[m + 2] - edges[m + 1]);
            bank[m][b] = std::max(0.0, std::min(up, down));
        }
    return bank;
}

struct Features {
    std::vector<double> centroid, spread, rolloff, flatness, flux;
};

void spectral_frame(Features& out, std::span<const double> power, std::vector<double>& previous, double rate, std::size_t n, double pct) {
    std::vector<double> m(power.size());
    double total = 0;
    for (std::size_t k = 0; k < power.size(); ++k) total += m[k] = std::sqrt(power[k]);
    if (!previous.empty()) {
        double sq = 0;
        for (std::size_t k = 0; k < m.size(); ++k) sq += (m[k] - previous[k]) * (m[k] - previous[k]);
        out.flux.push_back(std::sqrt(sq));
    }
    previous = m;
    if (!(total > 0)) return;
    const auto freq = [&](std::size_t k) { return static_cast<double>(k) * rate / static_cast<double>(n); };
    double moment = 0;
    for (std::size_t k = 0; k < m.size(); ++k) moment += freq(k) * m[k];
    const double centroid = moment / total;
    double spread = 0, cumulative = 0, logs = 0, sum = 0;
    std::optional<double> rolloff;
    for (std::size_t k = 0; k < m.size(); ++k) {
        spread += (freq(k) - centroid) * (freq(k) - centroid) * m[k];
        cumulative += m[k];
        if (!rolloff && cumulative >= pct / 100 * total) rolloff = freq(k);
        const double floored = std::max(power[k], kPowerFloor);
        logs += std::log(floored);
        sum += floored;
    }
    const auto count = static_cast<double>(m.size());
    out.centroid.push_back(centroid);
    out.spread.push_back(std::sqrt(spread / total));
    out.rolloff.push_back(rolloff.value_or(freq(m.size() - 1)));
    out.flatness.push_back(std::exp(logs / count) / (sum / count));
}

std::vector<double> ranks(const Samples& x) {
    std::vector<std::size_t> order(x.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return x[i]; });
    std::vector<double> r(x.size());
    for (std::size_t i = 0; i < order.size();) {
        auto j = i;
        while (j + 1 < order.size() && x[order[j + 1]] == x[order[i]]) ++j;
        const double average = 0.5 * static_cast<double>(i + j) + 1;
        for (auto k = i; k <= j; ++k) r[order[k]] = average;
        i = j + 1;
    }
    return r;
}

double pearson(std::span<const double> a, std::span<const double> b) {
    const auto n = static_cast<double>(a.size());
    const double ma = std::accumulate(a.begin(), a.end(), 0.0) / n, mb = std::accumulate(b.begin(), b.end(), 0.0) / n;
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? std::clamp(sab / std::sqrt(saa * sbb), -1.0, 1.0) : 0.0;
}

core::Result<std::optional<std::pair<double, double>>> histogram_range(const std::string& text) {
    if (text == "auto") return std::nullopt;
    auto v = parse_numbers("histogram", "range", text);
    if (!v || v->size() != 2 || !((*v)[0] < (*v)[1])) return fail(tr("pipeline.error.histogram_range"), fmt::arg("value", text));
    return std::pair{(*v)[0], (*v)[1]};
}

void histogram_channel(Report& r, const Samples& x, std::size_t bins, std::optional<std::pair<double, double>> range, const std::string& sfx) {
    double lo = 0, hi = 1;
    if (range) {
        std::tie(lo, hi) = *range;
    } else if (!x.empty()) {
        const auto [smallest, largest] = std::ranges::minmax(x);
        lo = smallest, hi = largest;
    }
    if (lo == hi) lo -= 0.5, hi += 0.5;
    const double scale = std::isfinite(hi - lo) ? 1.0 : 0.5;
    const double span = hi * scale - lo * scale, step = span / static_cast<double>(bins);
    std::vector<double> edges(bins + 1);
    for (std::size_t i = 0; i < bins; ++i) edges[i] = (static_cast<double>(i) * step + lo * scale) / scale;
    edges[bins] = hi;
    std::vector<std::size_t> counts(bins, 0);
    std::size_t inside = 0;
    for (const double v : x) {
        if (!(v >= lo && v <= hi)) continue;
        const double position = (v * scale - lo * scale) / span * static_cast<double>(bins);
        if (!std::isfinite(position)) continue;
        auto k = static_cast<std::size_t>(std::clamp(position, 0.0, static_cast<double>(bins - 1)));
        if (k > 0 && v < edges[k]) --k;
        if (k + 1 < bins && v >= edges[k + 1]) ++k;
        ++counts[k];
        ++inside;
    }
    std::string list;
    for (const auto c : counts) list += (list.empty() ? "" : " ") + std::to_string(c);
    const auto fullest = static_cast<std::size_t>(std::ranges::max_element(counts) - counts.begin());
    r.fields.push_back({"lo" + sfx, lo});
    r.fields.push_back({"hi" + sfx, hi});
    r.fields.push_back({"counts" + sfx, list});
    r.fields.push_back({"in_range" + sfx, static_cast<double>(inside)});
    r.fields.push_back({"outside" + sfx, static_cast<double>(x.size() - inside)});
    r.fields.push_back({"mode" + sfx, (edges[fullest] + edges[fullest + 1]) / 2});
}

double entropy_bits(const Samples& band) {
    const double e = core::energy(band);
    if (!(e > 0)) return 0.0;
    double h = 0;
    for (const double c : band)
        if (const double p = c * c / e; p > 0) h -= p * std::log2(p);
    return h;
}

}  // namespace

void register_analysis_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"pitch",
                           kIn,
                           {{"report", PortType::report}, {"f0", PortType::signal}},
                           {{"fmin", Kind::real, "60", {}, 1, 20000},
                            {"fmax", Kind::real, "1000", {}, 1, 100000},
                            {"frame", Kind::integer, "2048", {}, 32, 65536},
                            {"hop", Kind::integer, "512", {}, 1, 65536},
                            {"threshold", Kind::real, "0.1", {}, 0.001, 1}},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = needs_rate("pitch", a)) return std::unexpected{*e};
                               if (auto e = finite_input("pitch", a)) return std::unexpected{*e};
                               const double fmin = param_real(p, "fmin"), fmax = param_real(p, "fmax");
                               if (!(fmin < fmax)) return fail(tr("pipeline.error.pitch_range"), fmt::arg("fmin", fmin), fmt::arg("fmax", fmax));
                               const core::YinOptions o{static_cast<std::size_t>(param_int(p, "frame")), static_cast<std::size_t>(param_int(p, "hop")), fmin, fmax,
                                                        param_real(p, "threshold")};
                               if (!(a.sample_rate / fmin <= kMaxSamples)) return fail(tr("pipeline.error.rate_too_high"), fmt::arg("block", "pitch"), fmt::arg("rate", a.sample_rate));
                               const auto tau_max = static_cast<std::size_t>(std::ceil(a.sample_rate / fmin));
                               if (tau_max > o.frame - o.frame / 2)
                                   return fail(tr("pipeline.error.pitch_frame"), fmt::arg("frame", o.frame), fmt::arg("fmin", fmin), fmt::arg("rate", a.sample_rate),
                                               fmt::arg("min", 2 * tau_max));
                               Report r;
                               core::AudioBuffer track;
                               track.sample_rate = a.sample_rate / static_cast<double>(o.hop);
                               const auto count = static_cast<double>(std::max<std::size_t>(1, a.channels.size()));
                               for (std::size_t c = 0; c < a.channels.size(); ++c) {
                                   ScaledProgress progress{ctx.progress, static_cast<double>(c) / count, 1 / count};
                                   auto f0 = core::yin_track(a.channels[c], a.sample_rate, o, &progress);
                                   if (!f0) return std::unexpected{f0.error()};
                                   const auto voiced = static_cast<std::size_t>(std::ranges::count_if(*f0, [](double f) { return f > 0; }));
                                   const auto sfx = suffix(a, c);
                                   r.fields.push_back({"f0_median_hz" + sfx, core::median_f0(*f0)});
                                   r.fields.push_back({"voiced_fraction" + sfx, f0->empty() ? 0.0 : static_cast<double>(voiced) / static_cast<double>(f0->size())});
                                   r.fields.push_back({"frames" + sfx, static_cast<double>(f0->size())});
                                   track.channels.push_back(std::move(*f0));
                               }
                               return std::vector<Value>{std::move(r), std::move(track)};
                           }}));
    out.push_back(checked({"mfcc",
                           kIn,
                           kReportOut,
                           {{"n_mfcc", Kind::integer, "13", {}, 1, 128},
                            {"n_mels", Kind::integer, "40", {}, 1, 256},
                            {"frame", Kind::integer, "2048", {}, 16, 65536},
                            {"hop", Kind::integer, "512", {}, 1, 65536}},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = needs_rate("mfcc", a)) return std::unexpected{*e};
                               if (auto e = finite_input("mfcc", a)) return std::unexpected{*e};
                               const auto count = static_cast<std::size_t>(param_int(p, "n_mfcc")), mels = static_cast<std::size_t>(param_int(p, "n_mels"));
                               const auto n = static_cast<std::size_t>(param_int(p, "frame")), hop = static_cast<std::size_t>(param_int(p, "hop"));
                               if (count > mels) return fail(tr("pipeline.error.mfcc_count"), fmt::arg("n", count), fmt::arg("mels", mels));
                               const auto bank = mel_bank(a.sample_rate, n, mels);
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c) {
                                   std::vector<double> sums(count, 0.0), energies(mels);
                                   std::size_t frames = 0;
                                   if (a.channels[c].size() >= n) {
                                       auto done = core::stft_frames(a.channels[c], n, hop, [&](std::size_t, std::span<const double> power) {
                                           for (std::size_t m = 0; m < mels; ++m) {
                                               double e = 0;
                                               for (std::size_t b = 0; b < power.size(); ++b) e += bank[m][b] * power[b];
                                               energies[m] = 10 * std::log10(std::max(e, kPowerFloor));
                                           }
                                           const auto coeffs = core::dct2(energies);
                                           for (std::size_t k = 0; k < count; ++k) sums[k] += coeffs[k];
                                       }, ctx.progress);
                                       if (!done) return std::unexpected{done.error()};
                                       frames = *done;
                                   }
                                   const auto sfx = suffix(a, c);
                                   r.fields.push_back({"frames" + sfx, static_cast<double>(frames)});
                                   for (std::size_t k = 0; k < count; ++k)
                                       r.fields.push_back({"mfcc" + std::to_string(k) + sfx, frames ? sums[k] / static_cast<double>(frames) : 0.0});
                               }
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"spectral_features",
                           kIn,
                           kReportOut,
                           {{"frame", Kind::integer, "2048", {}, 16, 65536}, {"hop", Kind::integer, "512", {}, 1, 65536}, {"rolloff_pct", Kind::real, "85", {}, 1, 99}},
                           [](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = needs_rate("spectral_features", a)) return std::unexpected{*e};
                               if (auto e = finite_input("spectral_features", a)) return std::unexpected{*e};
                               const auto n = static_cast<std::size_t>(param_int(p, "frame")), hop = static_cast<std::size_t>(param_int(p, "hop"));
                               const double pct = param_real(p, "rolloff_pct");
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c) {
                                   Features f;
                                   std::vector<double> previous;
                                   std::size_t frames = 0;
                                   if (a.channels[c].size() >= n) {
                                       auto done = core::stft_frames(a.channels[c], n, hop, [&](std::size_t, std::span<const double> power) {
                                           spectral_frame(f, power, previous, a.sample_rate, n, pct);
                                       }, ctx.progress);
                                       if (!done) return std::unexpected{done.error()};
                                       frames = *done;
                                   }
                                   const auto sfx = suffix(a, c);
                                   r.fields.push_back({"centroid_hz" + sfx, mean(f.centroid)});
                                   r.fields.push_back({"spread_hz" + sfx, mean(f.spread)});
                                   r.fields.push_back({"rolloff_hz" + sfx, mean(f.rolloff)});
                                   r.fields.push_back({"flatness" + sfx, mean(f.flatness)});
                                   r.fields.push_back({"flux" + sfx, mean(f.flux)});
                                   r.fields.push_back({"frames" + sfx, static_cast<double>(frames)});
                               }
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"histogram",
                           kIn,
                           kReportOut,
                           {{"bins", Kind::integer, "10", {}, 1, 1024}, {"range", Kind::text, "auto"}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               if (auto e = finite_input("histogram", a)) return std::unexpected{*e};
                               const auto range = histogram_range(param_text(p, "range"));
                               if (!range) return std::unexpected{range.error()};
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c)
                                   histogram_channel(r, a.channels[c], static_cast<std::size_t>(param_int(p, "bins")), *range, suffix(a, c));
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"delay_estimate", kPair, kReportOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& a = audio(in, 0);
                               const auto& b = audio(in, 1);
                               if (auto e = pair_checks("delay_estimate", a, b)) return std::unexpected{*e};
                               if (auto e = needs_rate("delay_estimate", a)) return std::unexpected{*e};
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c) {
                                   const auto& x = a.channels[c];
                                   const auto& y = *channel_or_last(b, c);
                                   const auto corr = core::correlate(y, x, core::CorrelationMode::full);
                                   if (!corr) return std::unexpected{corr.error()};
                                   const auto& z = *corr;
                                   const double energies = core::energy(x) * core::energy(y);
                                   double lag = 0, peak_lag = 0, peak = 0;
                                   if (energies > 0) {
                                       std::size_t k = 0;
                                       for (std::size_t i = 1; i < z.size(); ++i)
                                           if (std::abs(z[i]) > std::abs(z[k])) k = i;
                                       double delta = 0;
                                       if (k > 0 && k + 1 < z.size()) {
                                           const double y0 = std::abs(z[k - 1]), y1 = std::abs(z[k]), y2 = std::abs(z[k + 1]);
                                           if (y0 - 2 * y1 + y2 < 0) delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
                                       }
                                       peak_lag = static_cast<double>(k) - static_cast<double>(x.size() - 1);
                                       lag = peak_lag + delta;
                                       peak = z[k] / std::sqrt(energies);
                                   }
                                   const auto sfx = suffix(a, c);
                                   r.fields.push_back({"lag_samples" + sfx, lag});
                                   r.fields.push_back({"lag_s" + sfx, lag / a.sample_rate});
                                   r.fields.push_back({"peak_lag" + sfx, peak_lag});
                                   r.fields.push_back({"peak_corr" + sfx, peak});
                               }
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"correlation", kPair, kReportOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               const auto& a = audio(in, 0);
                               const auto& b = audio(in, 1);
                               if (auto e = pair_checks("correlation", a, b)) return std::unexpected{*e};
                               Report r;
                               for (std::size_t c = 0; c < a.channels.size(); ++c) {
                                   const auto& y = *channel_or_last(b, c);
                                   const auto n = std::min(a.channels[c].size(), y.size());
                                   const Samples x(a.channels[c].begin(), a.channels[c].begin() + static_cast<std::ptrdiff_t>(n));
                                   const Samples z(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(n));
                                   const double energies = core::energy(x) * core::energy(z);
                                   double dot = 0;
                                   for (std::size_t i = 0; i < n; ++i) dot += x[i] * z[i];
                                   const auto sfx = suffix(a, c);
                                   r.fields.push_back({"pearson" + sfx, pearson(x, z)});
                                   r.fields.push_back({"spearman" + sfx, pearson(ranks(x), ranks(z))});
                                   r.fields.push_back({"cosine" + sfx, energies > 0 ? std::clamp(dot / std::sqrt(energies), -1.0, 1.0) : 0.0});
                                   r.fields.push_back({"n" + sfx, static_cast<double>(n)});
                               }
                               return one(std::move(r));
                           }}));
    out.push_back(checked({"coeff_stats", kCoeffsIn, kReportOut, {{"tol", Kind::real, "0.01", {}, 0, 1}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& c = coeffs(in);
                               const double tol = param_real(p, "tol");
                               Report r;
                               for (std::size_t ch = 0; ch < c.channels.size(); ++ch) {
                                   const auto& bands = c.channels[ch].bands;
                                   if (bands.empty()) continue;
                                   const auto levels = bands.size() - 1;
                                   const auto shares = core::band_energy_fractions(c.channels[ch], c.transform == "swt" ? core::swt_energy_weights(levels, c.norm) : std::vector<double>{});
                                   double peak = 0;
                                   for (const auto& band : bands)
                                       for (const double v : band) peak = std::max(peak, std::abs(v));
                                   for (std::size_t b = 0; b < bands.size(); ++b) {
                                       const auto name = "c" + std::to_string(ch) + "." + band_label(c, b);
                                       const auto small = std::ranges::count_if(bands[b], [&](double v) { return std::abs(v) < tol * peak; });
                                       const double sparsity = bands[b].empty() ? 0.0 : peak > 0 ? static_cast<double>(small) / static_cast<double>(bands[b].size()) : 1.0;
                                       r.fields.push_back({name + ".energy", shares[b]});
                                       r.fields.push_back({name + ".entropy", entropy_bits(bands[b])});
                                       r.fields.push_back({name + ".sparsity", sparsity});
                                   }
                               }
                               return one(std::move(r));
                           }}));
}

}  // namespace signals::pipeline
