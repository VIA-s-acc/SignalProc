#include "learn/measure.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fmt/format.h>
#include <limits>
#include <numbers>

#include "core/analysis.hpp"
#include "core/expr.hpp"
#include "core/metrics.hpp"
#include "i18n/i18n.hpp"
#include "learn/structure.hpp"

namespace signals::learn {

namespace {

using pipeline::Value;
constexpr double kInf = std::numeric_limits<double>::infinity();

std::unexpected<core::Error> fail(std::string_view key, std::string_view endpoint, std::string_view extra = {}) {
    return core::make_error(core::Errc::invalid_argument,
                            fmt::format(fmt::runtime(tr(key)), fmt::arg("endpoint", endpoint), fmt::arg("extra", extra)));
}

pipeline::PortRef port_of(std::string_view endpoint) {
    const auto dot = endpoint.find('.');
    if (dot == std::string_view::npos) return {std::string{endpoint}, {}};
    return {std::string{endpoint.substr(0, dot)}, std::string{endpoint.substr(dot + 1)}};
}

core::Result<const Value*> value_of(const pipeline::RunResult& run, std::string_view endpoint) {
    const auto it = run.probes.find(probe_name(endpoint));
    if (it == run.probes.end()) return fail("learn.error.no_output", endpoint);
    return &it->second;
}

core::Result<core::AudioBuffer> signal_of(const pipeline::RunResult& run, std::string_view endpoint) {
    auto v = value_of(run, endpoint);
    if (!v) return std::unexpected{v.error()};
    if (const auto* a = std::get_if<core::AudioBuffer>(*v)) return *a;
    if (const auto* d = std::get_if<double>(*v)) return core::AudioBuffer{0, {{*d}}};
    return fail("learn.error.not_signal", endpoint);
}

struct Pair {
    core::AudioBuffer a, b;
};

core::Result<Pair> pair_of(const Measure& m, const pipeline::RunResult& run, const pipeline::RunResult* reference) {
    auto a = signal_of(run, m.a);
    if (!a) return std::unexpected{a.error()};
    if (!m.b.empty()) {
        auto b = signal_of(run, m.b);
        if (!b) return std::unexpected{b.error()};
        return Pair{std::move(*a), std::move(*b)};
    }
    if (!reference) return fail("learn.error.no_reference", m.ref);
    auto b = signal_of(*reference, m.ref);
    if (!b) return std::unexpected{b.error()};
    return Pair{std::move(*a), std::move(*b)};
}

core::Result<std::size_t> shift_of(const Measure& m, const Pair& p) {
    if (m.delay == "auto") {
        if (p.a.channels.empty() || p.b.channels.empty()) return 0;
        return core::best_shift(p.b.channels[0], p.a.channels[0], 4096);
    }
    auto v = core::eval_number(m.delay);
    if (!v || *v < 0 || *v != std::floor(*v)) return fail("learn.error.bad_delay", m.name, m.delay);
    return static_cast<std::size_t>(*v);
}

struct Aligned {
    bool covered = true;
    double max_error = 0, snr = kInf;
};

Aligned align(const Pair& p, std::size_t shift, std::size_t trim) {
    Aligned out;
    if (p.a.channels.size() != p.b.channels.size() || p.b.channels.empty()) return {false, kInf, -kInf};
    for (std::size_t ch = 0; ch < p.b.channels.size(); ++ch) {
        const auto& ref = p.b.channels[ch];
        const auto& test = p.a.channels[ch];
        if (test.size() < shift + ref.size() || ref.size() <= 2 * trim) return {false, kInf, -kInf};
        const auto n = ref.size() - 2 * trim;
        const std::span<const double> r{ref.data() + trim, n}, t{test.data() + shift + trim, n};
        out.max_error = std::max(out.max_error, core::max_abs_error(r, t));
        out.snr = std::min(out.snr, core::snr_db(r, t));
    }
    return out;
}

double tone_level(const core::AudioBuffer& s, double freq) {
    if (s.channels.empty() || s.channels[0].size() < 2 || s.sample_rate <= 0) return -kInf;
    const auto& x = s.channels[0];
    const auto n = x.size();
    const double w = 2 * std::numbers::pi * freq / s.sample_rate;
    std::complex<double> acc{0, 0};
    double wsum = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double hann = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n - 1));
        acc += hann * x[k] * std::polar(1.0, -w * static_cast<double>(k));
        wsum += hann;
    }
    return 20 * std::log10(2 * std::abs(acc) / wsum);
}

std::vector<double> all_samples(const core::AudioBuffer& s) {
    std::vector<double> out;
    for (const auto& ch : s.channels) out.insert(out.end(), ch.begin(), ch.end());
    return out;
}

std::optional<double> report_field(const pipeline::RunResult& run, std::string_view node, std::string_view field) {
    for (const auto& r : run.reports)
        if (r.node == node)
            for (const auto& [name, value] : r.report.fields)
                if (name == field)
                    if (const auto* d = std::get_if<double>(&value)) return *d;
    return std::nullopt;
}

}  // namespace

std::string probe_name(std::string_view endpoint) { return "__learn:" + std::string{endpoint}; }

void add_measure_probes(pipeline::PipelineDoc& doc, std::span<const Measure> measures, bool reference) {
    const auto add = [&](const std::string& endpoint) {
        if (endpoint.empty()) return;
        const auto name = probe_name(endpoint);
        if (std::ranges::any_of(doc.probes, [&](const auto& p) { return p.name == name; })) return;
        if (std::ranges::none_of(doc.nodes, [&](const auto& n) { return n.id == port_of(endpoint).node; })) return;
        doc.probes.push_back({port_of(endpoint), name});
    };
    static constexpr std::string_view kSignalKinds[] = {"max_error", "snr", "delay", "tone_db", "gain_db", "max_abs", "energy", "rms_db", "length", "kept", "samples"};
    for (const auto& m : measures) {
        if (std::ranges::find(kSignalKinds, m.kind) == std::end(kSignalKinds)) continue;
        if (reference) {
            add(m.ref);
        } else {
            add(m.a);
            add(m.b);
        }
    }
}

double MeasureValue::scalar() const { return values.empty() ? std::numeric_limits<double>::quiet_NaN() : values.front(); }

core::Result<MeasureValue> compute_measure(const Measure& m, const pipeline::PipelineDoc& doc, const pipeline::RunResult& run,
                                           const pipeline::RunResult* reference) {
    const auto one = [](double v) { return MeasureValue{{v}}; };
    if (m.kind == "count") return one(static_cast<double>(count_blocks(doc, m.block)));
    if (m.kind == "param") {
        const auto at = port_of(m.param);
        for (const auto& n : doc.nodes)
            if (n.id == at.node)
                if (const auto it = n.params.find(at.port); it != n.params.end())
                    if (auto v = core::eval_number(it->second)) return one(*v);
        return fail("learn.error.no_param", m.param);
    }
    if (m.kind == "field") {
        if (auto v = report_field(run, m.a, m.field)) return one(*v);
        return fail("learn.error.no_field", m.a, m.field);
    }
    if (m.kind == "kept") {
        auto v = value_of(run, m.a);
        if (!v) return std::unexpected{v.error()};
        const auto* c = std::get_if<pipeline::Coeffs>(*v);
        if (!c) return fail("learn.error.not_coeffs", m.a);
        double total = 0, nonzero = 0;
        for (const auto& ch : c->channels)
            for (const auto& band : ch.bands)
                for (double x : band) {
                    total += 1;
                    nonzero += x != 0;
                }
        return one(total > 0 ? nonzero / total : 0);
    }
    if (m.kind == "max_error" || m.kind == "snr" || m.kind == "delay" || m.kind == "gain_db") {
        auto p = pair_of(m, run, reference);
        if (!p) return std::unexpected{p.error()};
        if (m.kind == "gain_db") return one(tone_level(p->a, m.freq) - tone_level(p->b, m.freq));
        if (m.kind == "delay") {
            if (p->a.channels.empty() || p->b.channels.empty() || p->a.channels[0].empty() || p->b.channels[0].empty()) return one(0);
            auto c = core::correlate(p->a.channels[0], p->b.channels[0], core::CorrelationMode::full);
            if (!c) return std::unexpected{c.error()};
            const auto zero = static_cast<long long>(p->b.channels[0].size()) - 1;
            long long best = 0;
            double peak = -kInf;
            for (std::size_t k = 0; k < c->size(); ++k) {
                const auto lag = static_cast<long long>(k) - zero;
                if (std::llabs(lag) <= 4096 && (*c)[k] > peak) peak = (*c)[k], best = lag;
            }
            return one(static_cast<double>(best));
        }
        auto shift = shift_of(m, *p);
        if (!shift) return std::unexpected{shift.error()};
        const auto aligned = align(*p, *shift, m.trim);
        return one(m.kind == "snr" ? aligned.snr : aligned.max_error);
    }
    auto s = signal_of(run, m.a);
    if (!s) return std::unexpected{s.error()};
    if (m.kind == "tone_db") return one(tone_level(*s, m.freq));
    if (m.kind == "samples") return MeasureValue{s->channels.empty() ? std::vector<double>{} : s->channels[0]};
    if (m.kind == "length") return one(static_cast<double>(s->frames()));
    const auto x = all_samples(*s);
    if (m.kind == "max_abs") {
        double peak = 0;
        for (double v : x) peak = std::max(peak, std::abs(v));
        return one(peak);
    }
    const double energy = core::energy(x);
    if (m.kind == "energy") return one(energy);
    if (m.kind == "rms_db") return one(x.empty() ? -kInf : 10 * std::log10(energy / static_cast<double>(x.size())));
    return fail("learn.error.bad_measure", m.name, m.kind);
}

}  // namespace signals::learn
