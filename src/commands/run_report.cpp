#include "commands/run_report.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "core/spectral.hpp"
#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"
#include "pipeline/layout.hpp"
#include "svg/svg.hpp"

namespace signals::cli {
namespace {

constexpr std::array<const char*, 8> kColours{"#0072B2", "#D55E00", "#009E73", "#E69F00", "#56B4E9", "#CC79A7", "#B8A200", "#000000"};
constexpr std::size_t kExactFrames = 2000, kStemFrames = 64, kListedFrames = 64, kColumns = 1200;

std::string slug(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c >= 0x80 || c == '-' || c == '_') out += static_cast<char>(c);
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string{"probe"} : out;
}

std::string channel_name(std::size_t c, std::size_t channels) { return channels == 1 ? std::string{"x"} : "ch" + std::to_string(c); }

std::string cell(double v) { return fmt::format("{:.6g}", v); }

svg::Chart time_chart(const core::AudioBuffer& a, const std::string& title) {
    svg::Chart c;
    c.title = title;
    const double rate = a.sample_rate > 0 ? a.sample_rate : 1.0;
    const auto frames = a.frames();
    c.x = {0, static_cast<double>(frames > 1 ? frames - 1 : 1) / rate, svg::Scale::linear, tr("pipeline.report.time_s")};
    double peak = 1e-12;
    for (const auto& ch : a.channels)
        for (double v : ch) peak = std::max(peak, std::abs(v));
    c.y = {-1.05 * peak, 1.05 * peak};
    for (std::size_t k = 0; k < a.channels.size(); ++k) {
        const auto& ch = a.channels[k];
        svg::Series s{channel_name(k, a.channels.size()), kColours[k % kColours.size()]};
        if (frames <= kExactFrames) {
            for (std::size_t n = 0; n < frames; ++n) s.x.push_back(static_cast<double>(n) / rate);
            s.y = ch;
            s.stems = frames <= kStemFrames;
        } else {
            for (std::size_t col = 0; col < kColumns; ++col) {
                const auto from = col * frames / kColumns, to = std::max(from + 1, (col + 1) * frames / kColumns);
                const auto [lo, hi] = std::minmax_element(ch.begin() + static_cast<std::ptrdiff_t>(from), ch.begin() + static_cast<std::ptrdiff_t>(to));
                s.lo.push_back(*lo);
                s.hi.push_back(*hi);
            }
        }
        c.series.push_back(std::move(s));
    }
    return c;
}

std::optional<svg::Chart> spectrum_chart(const core::AudioBuffer& a, const std::string& title) {
    if (a.frames() < 16 || !(a.sample_rate > 0)) return std::nullopt;
    std::size_t segment = 4096;
    while (segment > 16 && segment > a.frames()) segment /= 2;
    const auto freqs = core::welch_frequencies(a.sample_rate, segment);
    svg::Chart c;
    c.title = title;
    double top = -1e300;
    for (std::size_t k = 0; k < a.channels.size(); ++k) {
        auto psd = core::welch_psd(a.channels[k], a.sample_rate, segment, segment / 2);
        if (!psd) continue;
        svg::Series s{channel_name(k, a.channels.size()), kColours[k % kColours.size()]};
        for (std::size_t j = 1; j < psd->size() && j < freqs.size(); ++j) {
            s.x.push_back(freqs[j]);
            s.y.push_back(10 * std::log10(std::max((*psd)[j], 1e-30)));
            top = std::max(top, s.y.back());
        }
        c.series.push_back(std::move(s));
    }
    if (c.series.empty() || freqs.size() < 2) return std::nullopt;
    c.x = {freqs[1], a.sample_rate / 2, svg::Scale::log10, tr("pipeline.report.freq_hz")};
    c.y = {top - 100, top + 3, svg::Scale::linear, tr("pipeline.report.db")};
    return c;
}

core::Result<std::string> plot(const svg::Chart& chart, const std::filesystem::path& folder, const std::string& folder_name, const std::string& stem) {
    if (auto r = io::write_file_atomic(folder / (stem + ".svg"), svg::to_svg(chart)); !r) return std::unexpected{r.error()};
    if (auto r = io::write_file_atomic(folder / (stem + ".csv"), svg::to_csv(chart)); !r) return std::unexpected{r.error()};
    return "![" + chart.title + "](" + folder_name + "/" + stem + ".svg)\n\n";
}

std::string field_table(const pipeline::Report& report) {
    std::string out = "| " + tr("pipeline.report.field") + " | " + tr("pipeline.report.value") + " |\n|---|---|\n";
    for (const auto& [name, value] : report.fields)
        out += "| " + name + " | " + std::visit([](const auto& x) {
                   if constexpr (std::is_same_v<std::decay_t<decltype(x)>, double>) return cell(x);
                   else return std::string{x};
               },
                                              value) +
               " |\n";
    return out + "\n";
}

}  // namespace

core::Result<void> write_run_report(const std::filesystem::path& report, const pipeline::PipelineDoc& doc, const pipeline::Graph& graph,
                                    const pipeline::RunResult& result, const std::filesystem::path& sig_file) {
    const auto dir = report.parent_path().empty() ? std::filesystem::path{"."} : report.parent_path();
    const auto folder_name = report.stem().string() + "_files";
    const auto folder = dir / folder_name;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return core::make_error(core::Errc::io_error, fmt::format(fmt::runtime(tr("pipeline.report.cannot_create")), fmt::arg("path", folder.string())));
    std::string md = "# " + fmt::format(fmt::runtime(tr("pipeline.report.title")), fmt::arg("name", doc.name.empty() ? sig_file.stem().string() : doc.name)) + "\n\n";
    md += fmt::format(fmt::runtime(tr("pipeline.report.source")), fmt::arg("file", sig_file.filename().string())) + "\n\n";
    md += "## " + tr("pipeline.report.graph") + "\n\n```\n";
    for (const auto& line : pipeline::draw_graph(graph, false)) md += line + "\n";
    md += "```\n\n## " + tr("pipeline.report.nodes") + "\n\n";
    md += "| " + tr("pipeline.report.node") + " | " + tr("pipeline.report.block") + " | " + tr("pipeline.report.state") + " | " + tr("pipeline.report.shape") + " | " +
          tr("pipeline.report.time_ms") + " |\n|---|---|---|---|---|\n";
    for (const auto i : graph.order) {
        const auto& s = result.nodes[i];
        std::string shape;
        for (const auto& o : s.shapes) {
            const bool sized = o.type == pipeline::PortType::signal || o.type == pipeline::PortType::coeffs;
            shape += (shape.empty() ? "" : ", ") + (sized ? fmt::format("{}×{}", o.channels, o.frames) : std::string{pipeline::to_string(o.type)});
        }
        md += fmt::format("| {} | {} | {} | {} | {:.2f} |\n", s.id, graph.nodes[i].type->id, pipeline::to_string(s.state), shape, s.wall_seconds * 1e3);
    }
    md += "\n";
    if (!result.probes.empty()) md += "## " + tr("pipeline.report.probes") + "\n\n";
    for (const auto& [name, value] : result.probes) {
        md += "### " + name + "\n\n";
        if (const auto* a = std::get_if<core::AudioBuffer>(&value)) {
            md += fmt::format(fmt::runtime(tr("pipeline.report.signal")), fmt::arg("channels", a->channels.size()), fmt::arg("frames", a->frames()),
                              fmt::arg("rate", cell(a->sample_rate))) +
                  "\n\n";
            auto time = plot(time_chart(*a, name + " · " + tr("pipeline.report.time")), folder, folder_name, slug(name) + "_time");
            if (!time) return std::unexpected{time.error()};
            md += *time;
            if (const auto chart = spectrum_chart(*a, name + " · " + tr("pipeline.report.spectrum"))) {
                auto spectrum = plot(*chart, folder, folder_name, slug(name) + "_spectrum");
                if (!spectrum) return std::unexpected{spectrum.error()};
                md += *spectrum;
            }
            if (a->frames() <= kListedFrames) {
                md += "| n |";
                for (std::size_t c = 0; c < a->channels.size(); ++c) md += " " + channel_name(c, a->channels.size()) + " |";
                md += "\n|---|";
                for (std::size_t c = 0; c < a->channels.size(); ++c) md += "---|";
                md += "\n";
                for (std::size_t n = 0; n < a->frames(); ++n) {
                    md += "| " + std::to_string(n) + " |";
                    for (const auto& ch : a->channels) md += " " + cell(ch[n]) + " |";
                    md += "\n";
                }
                md += "\n";
            }
        } else if (const auto* r = std::get_if<pipeline::Report>(&value)) {
            md += field_table(*r);
        } else if (const auto* d = std::get_if<double>(&value)) {
            md += cell(*d) + "\n\n";
        } else {
            md += std::string{pipeline::to_string(pipeline::type_of(value))} + "\n\n";
        }
    }
    if (!result.reports.empty()) md += "## " + tr("pipeline.report.reports") + "\n\n";
    for (const auto& nr : result.reports) md += "### " + nr.node + (nr.report.passed ? "" : " ✗") + "\n\n" + field_table(nr.report);
    md += (result.checks_passed ? "✓ " + tr("pipeline.report.passed") : "✗ " + tr("pipeline.report.failed")) + "\n";
    return io::write_file_atomic(report, md);
}

}  // namespace signals::cli
