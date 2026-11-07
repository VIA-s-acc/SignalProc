#include <algorithm>
#include <cmath>
#include <map>

#include "core/spectral.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

struct Lpc {
    Samples a, k;
    double gain_db = 0;
};

core::Result<Lpc> lpc_of(const Samples& x, std::size_t order) {
    Samples r(order + 1, 0.0);
    for (std::size_t lag = 0; lag <= order; ++lag)
        for (std::size_t n = lag; n < x.size(); ++n) r[lag] += x[n] * x[n - lag];
    Lpc out{Samples(order, 0.0), Samples(order, 0.0), 0.0};
    if (!(r[0] > 0)) return out;
    Samples a(order + 1, 0.0), previous;
    a[0] = 1;
    double error = r[0];
    for (std::size_t m = 1; m <= order; ++m) {
        double acc = r[m];
        for (std::size_t j = 1; j < m; ++j) acc += a[j] * r[m - j];
        const double k = -acc / error;
        previous = a;
        for (std::size_t j = 1; j < m; ++j) a[j] = previous[j] + k * previous[m - j];
        a[m] = k;
        error *= 1 - k * k;
        if (!(std::abs(k) < 1) || !(error > 0)) return fail(tr("pipeline.error.lpc_order"), fmt::arg("order", order));
        out.k[m - 1] = k;
    }
    std::copy(a.begin() + 1, a.end(), out.a.begin());
    out.gain_db = 10 * std::log10(r[0] / error);
    return out;
}

std::string suffix(std::size_t channel, std::size_t channels) { return channels > 1 ? "_ch" + std::to_string(channel) : std::string{}; }

Out lpc(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    const auto order = static_cast<std::size_t>(param_int(p, "order"));
    core::AudioBuffer residual;
    residual.sample_rate = a.sample_rate;
    Report r;
    r.fields.push_back({"order", static_cast<double>(order)});
    for (std::size_t ch = 0; ch < a.channels.size(); ++ch) {
        const auto& x = a.channels[ch];
        const auto model = lpc_of(x, order);
        if (!model) return std::unexpected{model.error()};
        Samples e(x.size());
        for (std::size_t n = 0; n < x.size(); ++n) {
            double acc = 0;
            for (std::size_t j = std::min(order, n); j >= 1; --j) acc += model->a[j - 1] * x[n - j];
            e[n] = x[n] + acc;
        }
        residual.channels.push_back(std::move(e));
        const auto tail = suffix(ch, a.channels.size());
        r.fields.push_back({"gain_db" + tail, model->gain_db});
        for (std::size_t j = 0; j < order; ++j) r.fields.push_back({"a" + std::to_string(j + 1) + tail, model->a[j]});
        for (std::size_t j = 0; j < order; ++j) r.fields.push_back({"k" + std::to_string(j + 1) + tail, model->k[j]});
    }
    return std::vector<Value>{std::move(residual), std::move(r)};
}

Out lpc_synth(std::span<const Value> in) {
    const auto& e = audio(in, 0);
    const auto& report = std::get<Report>(in[1]);
    std::map<std::string, double, std::less<>> fields;
    for (const auto& [name, value] : report.fields)
        if (const auto* d = std::get_if<double>(&value)) fields.emplace(name, *d);
    const auto order_field = fields.find("order");
    if (order_field == fields.end() || !(order_field->second >= 1 && order_field->second <= 64)) return fail(tr("pipeline.error.lpc_report"));
    const auto order = static_cast<std::size_t>(order_field->second);
    std::size_t channels = 0;
    if (fields.contains("a1")) channels = 1;
    else
        while (fields.contains("a1_ch" + std::to_string(channels))) ++channels;
    if (channels != e.channels.size()) return fail(tr("pipeline.error.lpc_channels"), fmt::arg("report", channels), fmt::arg("residual", e.channels.size()));
    core::AudioBuffer out;
    out.sample_rate = e.sample_rate;
    for (std::size_t ch = 0; ch < channels; ++ch) {
        Samples a(order);
        for (std::size_t j = 0; j < order; ++j) {
            const auto it = fields.find("a" + std::to_string(j + 1) + suffix(ch, channels));
            if (it == fields.end()) return fail(tr("pipeline.error.lpc_report"));
            a[j] = it->second;
        }
        const auto& x = e.channels[ch];
        Samples y(x.size());
        for (std::size_t n = 0; n < x.size(); ++n) {
            double acc = 0;
            for (std::size_t j = std::min(order, n); j >= 1; --j) acc += a[j - 1] * y[n - j];
            y[n] = x[n] - acc;
        }
        out.channels.push_back(std::move(y));
    }
    return one(std::move(out));
}

core::Result<Samples> psd_db(const Samples& x, double rate, std::size_t segment, std::vector<double>* raw = nullptr) {
    const auto length = std::min(x.size(), segment);
    if (length < 2) return Samples{};
    auto psd = core::welch_psd(x, rate, length, length / 2);
    if (!psd) return std::unexpected{psd.error()};
    Samples db(psd->size());
    for (std::size_t k = 0; k < db.size(); ++k) db[k] = 10 * std::log10(std::max((*psd)[k], 1e-20));
    if (raw) *raw = std::move(*psd);
    return db;
}

Out spectrum_peaks(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    if (!a.channels.empty() && !(a.sample_rate > 0)) return fail(tr("pipeline.error.no_rate"), fmt::arg("block", "spectrum_peaks"));
    const auto count = static_cast<std::size_t>(param_int(p, "count"));
    const auto segment = static_cast<std::size_t>(param_int(p, "segment"));
    Report r;
    for (std::size_t ch = 0; ch < a.channels.size(); ++ch) {
        const auto db = psd_db(a.channels[ch], a.sample_rate, segment);
        if (!db) return std::unexpected{db.error()};
        auto peaks = core::find_peaks(*db, param_real(p, "prominence_db"));
        std::ranges::stable_sort(peaks, std::greater<>{}, [&](const core::Peak& k) { return (*db)[k.index]; });
        peaks.resize(std::min(peaks.size(), count));
        const auto tail = suffix(ch, a.channels.size());
        const double bin = a.sample_rate / static_cast<double>(std::min(a.channels[ch].size(), segment));
        r.fields.push_back({"peaks" + tail, static_cast<double>(peaks.size())});
        for (std::size_t i = 0; i < peaks.size(); ++i) {
            r.fields.push_back({"peak" + std::to_string(i + 1) + "_hz" + tail, static_cast<double>(peaks[i].index) * bin});
            r.fields.push_back({"peak" + std::to_string(i + 1) + "_db" + tail, (*db)[peaks[i].index]});
        }
    }
    return one(std::move(r));
}

const std::vector<std::string> kOctaves{"31.5", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
const std::vector<std::string> kThirds{"25",  "31.5", "40",  "50", "63",   "80",   "100", "125",  "160", "200", "250", "315", "400",  "500", "630",
                                       "800", "1k",   "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k",  "6.3k", "8k", "10k", "12.5k", "16k", "20k"};

Out band_power(std::span<const Value> in, const Params& p) {
    const auto& a = audio(in);
    if (!a.channels.empty() && !(a.sample_rate > 0)) return fail(tr("pipeline.error.no_rate"), fmt::arg("block", "band_power"));
    const bool third = param_text(p, "bands") == "third";
    const auto& names = third ? kThirds : kOctaves;
    const double step = third ? 1 : 3;
    const double first = third ? -16 : -5;
    std::vector<double> edges(names.size() + 1);
    for (std::size_t i = 0; i < edges.size(); ++i) edges[i] = 1000 * std::pow(10.0, (step * (first + static_cast<double>(i)) - step / 2) / 10);
    std::size_t bands = 0;
    while (bands < names.size() && edges[bands] < a.sample_rate / 2) ++bands;
    const auto segment = static_cast<std::size_t>(param_int(p, "segment"));
    Report r;
    for (std::size_t ch = 0; ch < a.channels.size(); ++ch) {
        std::vector<double> psd;
        if (const auto db = psd_db(a.channels[ch], a.sample_rate, segment, &psd); !db) return std::unexpected{db.error()};
        std::vector<double> power(names.size() + 2, 0.0);
        double total = 0;
        const double bin = psd.empty() ? 0 : a.sample_rate / static_cast<double>(std::min(a.channels[ch].size(), segment));
        for (std::size_t k = 0; k < psd.size(); ++k) {
            const double f = static_cast<double>(k) * bin;
            const auto slot = static_cast<std::size_t>(std::ranges::upper_bound(edges, f) - edges.begin());
            power[slot] += psd[k];
            total += psd[k];
        }
        const auto share = [&](double v) { return total > 0 ? v / total : 0.0; };
        const auto tail = suffix(ch, a.channels.size());
        r.fields.push_back({"below_22hz" + tail, share(power.front())});
        for (std::size_t i = 0; i < bands; ++i) r.fields.push_back({names[i] + "hz" + tail, share(power[i + 1])});
        r.fields.push_back({"above_22khz" + tail, share(power[names.size() + 1])});
    }
    return one(std::move(r));
}

}  // namespace

void register_spectral_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"lpc",
                           kIn,
                           {{"residual", PortType::signal}, {"report", PortType::report}},
                           {{"order", Kind::integer, "12", {}, 1, 64}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return lpc(in, p); }}));
    out.push_back(checked({"lpc_synth", {{"residual", PortType::signal}, {"report", PortType::report}}, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return lpc_synth(in);
                           }}));
    out.push_back(checked({"spectrum_peaks",
                           kIn,
                           kReportOut,
                           {{"count", Kind::integer, "5", {}, 1, 64}, {"segment", Kind::integer, "4096", {}, 16, 65536}, {"prominence_db", Kind::real, "6", {}, 0, 200}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return spectrum_peaks(in, p); }}));
    out.push_back(checked({"band_power",
                           kIn,
                           kReportOut,
                           {{"bands", Kind::choice, "octave", {"octave", "third"}}, {"segment", Kind::integer, "4096", {}, 16, 65536}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) { return band_power(in, p); }}));
}

}  // namespace signals::pipeline
