#include "pipeline/sweep.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"

namespace signals::pipeline {
namespace {

constexpr std::size_t kMaxValues = 1000;
constexpr std::size_t kSweepCache = std::size_t{512} << 20;
constexpr std::string_view kProbePrefix = "__sweep_";

core::Error bad(std::string_view key, std::string_view text) {
    return {core::Errc::invalid_argument, fmt::format(fmt::runtime(tr(std::string{key})), fmt::arg("text", text))};
}

std::string trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return std::string{s};
}

std::string format_value(double v) {
    if (std::abs(v) < 1e-12) v = 0;
    return fmt::format("{:.10g}", v);
}

std::optional<std::vector<std::string>> range(std::string_view spec) {
    const auto at = spec.find('@');
    if (at != std::string_view::npos) {
        const auto colon = spec.find(':');
        if (colon == std::string_view::npos || colon > at) return std::nullopt;
        auto count_text = std::string{spec.substr(at + 1)};
        const bool log = count_text.ends_with("log");
        if (log) count_text.resize(count_text.size() - 3);
        const auto a = numeric(spec.substr(0, colon)), b = numeric(spec.substr(colon + 1, at - colon - 1)), n = numeric(count_text);
        if (!a || !b || !n || *n < 2 || *n != std::floor(*n) || (log && (*a <= 0 || *b <= 0))) return std::nullopt;
        const auto count = static_cast<std::size_t>(*n);
        if (count > kMaxValues) return std::vector<std::string>(count);
        std::vector<std::string> out;
        for (std::size_t k = 0; k < count; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(count - 1);
            out.push_back(format_value(log ? *a * std::pow(*b / *a, t) : *a + (*b - *a) * t));
        }
        return out;
    }
    const auto first = spec.find(':'), second = first == std::string_view::npos ? first : spec.find(':', first + 1);
    if (second == std::string_view::npos) return std::nullopt;
    const auto a = numeric(spec.substr(0, first)), b = numeric(spec.substr(first + 1, second - first - 1)), s = numeric(spec.substr(second + 1));
    if (!a || !b || !s || *s == 0 || (*b - *a) / *s < 0) return std::nullopt;
    const double steps = std::floor((*b - *a) / *s + 1e-9);
    if (steps + 1 > static_cast<double>(kMaxValues)) return std::vector<std::string>(static_cast<std::size_t>(std::min(steps + 1, 1e9)));
    std::vector<std::string> out;
    for (double k = 0; k <= steps; ++k) out.push_back(format_value(*a + k * *s));
    return out;
}

std::optional<double> metric_of(const Value& v, Metric m) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    std::vector<const std::vector<double>*> parts;
    std::size_t frames = 0;
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) {
        for (const auto& c : a->channels) parts.push_back(&c);
        frames = a->frames();
    } else if (const auto* c = std::get_if<Coeffs>(&v)) {
        for (const auto& ch : c->channels)
            for (const auto& band : ch.bands) parts.push_back(&band);
    } else {
        return std::nullopt;
    }
    double sum = 0, squares = 0, peak = 0;
    std::size_t count = 0;
    for (const auto* p : parts)
        for (double x : *p) {
            sum += x;
            squares += x * x;
            peak = std::max(peak, std::abs(x));
            ++count;
        }
    switch (m) {
    case Metric::rms: return count ? std::sqrt(squares / static_cast<double>(count)) : 0.0;
    case Metric::peak: return peak;
    case Metric::energy: return squares;
    case Metric::mean: return count ? sum / static_cast<double>(count) : 0.0;
    case Metric::length: return static_cast<double>(frames ? frames : count);
    }
    return std::nullopt;
}

void skip_writers(PipelineDoc& doc, const std::vector<Measure>& measures) {
    std::vector<std::string> gone;
    for (const auto& n : doc.nodes) {
        const auto* type = find_block_type(n.type);
        const bool measured = std::any_of(measures.begin(), measures.end(), [&](const Measure& m) { return m.node == n.id; });
        if (type && type->side_effect && !measured) gone.push_back(n.id);
    }
    const auto dropped = [&](const std::string& id) { return std::find(gone.begin(), gone.end(), id) != gone.end(); };
    std::erase_if(doc.nodes, [&](const NodeDecl& n) { return dropped(n.id); });
    std::erase_if(doc.edges, [&](const auto& e) { return dropped(e.from.node) || dropped(e.to.node); });
    std::erase_if(doc.probes, [&](const ProbeDecl& p) { return dropped(p.at.node); });
}

}  // namespace

std::optional<double> numeric(std::string_view value) {
    const auto text = trim(value);
    if (text.empty()) return std::nullopt;
    std::istringstream in{text};
    in.imbue(std::locale::classic());
    double v = 0;
    in >> v;
    if (in.fail() || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v)) return std::nullopt;
    return v;
}

core::Result<SweepAxis> parse_vary(std::string_view text) {
    const auto eq = text.find('=');
    const auto target = text.substr(0, eq);
    const auto dot = target.find('.');
    if (eq == std::string_view::npos || dot == std::string_view::npos || dot == 0 || dot + 1 == target.size() || eq + 1 == text.size())
        return std::unexpected{bad("pipeline.sweep.bad_vary", text)};
    SweepAxis axis{trim(target.substr(0, dot)), trim(target.substr(dot + 1)), {}};
    const auto spec = text.substr(eq + 1);
    if (spec.find(',') != std::string_view::npos) {
        for (std::size_t at = 0; at <= spec.size();) {
            const auto comma = std::min(spec.find(',', at), spec.size());
            auto item = trim(spec.substr(at, comma - at));
            if (item.empty()) return std::unexpected{bad("pipeline.sweep.bad_vary", text)};
            axis.values.push_back(std::move(item));
            at = comma + 1;
        }
    } else if (spec.find(':') != std::string_view::npos) {
        auto values = range(spec);
        if (!values) return std::unexpected{bad("pipeline.sweep.bad_vary", text)};
        if (values->size() > kMaxValues)
            return core::make_error(core::Errc::invalid_argument,
                                    fmt::format(fmt::runtime(tr("pipeline.sweep.too_many")), fmt::arg("count", values->size()), fmt::arg("limit", kMaxValues)));
        axis.values = std::move(*values);
    } else {
        axis.values.push_back(trim(spec));
    }
    return axis;
}

core::Result<Measure> parse_measure(std::string_view text) {
    Measure m{trim(text), {}};
    if (const auto colon = text.find(':'); colon != std::string_view::npos) {
        m.node = trim(text.substr(0, colon));
        const auto name = trim(text.substr(colon + 1));
        static const std::vector<std::pair<std::string_view, Metric>> kMetrics{
            {"rms", Metric::rms}, {"peak", Metric::peak}, {"energy", Metric::energy}, {"mean", Metric::mean}, {"length", Metric::length}};
        const auto it = std::find_if(kMetrics.begin(), kMetrics.end(), [&](const auto& p) { return p.first == name; });
        if (m.node.empty() || it == kMetrics.end()) return std::unexpected{bad("pipeline.sweep.bad_metric", text)};
        m.metric = it->second;
        return m;
    }
    const auto dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size()) return std::unexpected{bad("pipeline.sweep.bad_measure", text)};
    m.node = trim(text.substr(0, dot));
    m.field = trim(text.substr(dot + 1));
    return m;
}

core::Result<SweepResult> run_sweep(const PipelineDoc& doc, const std::filesystem::path& base_dir, SweepAxis axis, std::vector<Measure> measures,
                                    const core::CancelToken& cancel, const SweepProgress& progress) {
    const auto find_node = [&](std::string_view id) { return std::find_if(doc.nodes.begin(), doc.nodes.end(), [&](const NodeDecl& n) { return n.id == id; }); };
    const auto node = find_node(axis.node);
    if (node == doc.nodes.end()) return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.set.unknown_node")), fmt::arg("node", axis.node)));
    const auto* type = find_block_type(node->type);
    if (!type || std::none_of(type->params.begin(), type->params.end(), [&](const ParamSpec& p) { return p.name == axis.param; }))
        return core::make_error(core::Errc::invalid_argument,
                                fmt::format(fmt::runtime(tr("pipeline.sweep.unknown_param")), fmt::arg("node", axis.node), fmt::arg("param", axis.param)));
    for (const auto& m : measures)
        if (find_node(m.node) == doc.nodes.end())
            return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.set.unknown_node")), fmt::arg("node", m.node)));
    SweepResult result{std::move(axis), std::move(measures), {}};
    ResultCache cache{kSweepCache};
    const auto total = result.axis.values.size();
    for (std::size_t k = 0; k < total; ++k) {
        if (cancel.cancelled()) return core::make_error(core::Errc::cancelled, tr("pipeline.cli.cancelled"));
        const auto& value = result.axis.values[k];
        SweepRow row{value, std::vector<std::optional<double>>(result.measures.size()), {}};
        auto copy = doc;
        if (auto ok = apply_set(copy, result.axis.node + "." + result.axis.param + "=" + value); !ok) {
            row.error = ok.error().message;
        } else {
            skip_writers(copy, result.measures);
            for (std::size_t j = 0; j < result.measures.size(); ++j)
                if (result.measures[j].metric) copy.probes.push_back({PortRef{result.measures[j].node, {}, 0, 0}, std::string{kProbePrefix} + std::to_string(j)});
            auto graph = build_graph(copy, base_dir);
            if (!graph) {
                row.error = graph.error().message;
            } else if (auto run = run_graph(*graph, cache, {}, cancel); !run) {
                if (run.error().code == core::Errc::cancelled) return std::unexpected{run.error()};
                row.error = run.error().message;
            } else {
                for (std::size_t j = 0; j < result.measures.size(); ++j) {
                    const auto& m = result.measures[j];
                    if (m.metric) {
                        if (const auto it = run->probes.find(std::string{kProbePrefix} + std::to_string(j)); it != run->probes.end()) row.results[j] = metric_of(it->second, *m.metric);
                        continue;
                    }
                    for (const auto& nr : run->reports) {
                        if (nr.node != m.node) continue;
                        for (const auto& [name, field] : nr.report.fields)
                            if (name == m.field)
                                row.results[j] = std::holds_alternative<double>(field) ? std::optional{std::get<double>(field)} : numeric(std::get<std::string>(field));
                    }
                }
            }
        }
        result.rows.push_back(std::move(row));
        if (progress) progress(k + 1, total);
    }
    return result;
}

}  // namespace signals::pipeline
