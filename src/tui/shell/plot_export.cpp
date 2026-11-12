#include "tui/shell/plot_export.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"
#include "tui/shell/palette.hpp"

namespace signals::tui {
namespace {

constexpr int kEnvelopeColumns = 1200;

svg::Scale scale_of(AxisScale s) {
    switch (s) {
    case AxisScale::log10: return svg::Scale::log10;
    case AxisScale::pi: return svg::Scale::pi;
    case AxisScale::linear: break;
    }
    return svg::Scale::linear;
}

svg::Axis axis_of(const Axis& a, std::string label) { return {a.lo, a.hi, scale_of(a.scale), std::move(label)}; }

std::string colour(Rgb c, const Theme& theme, const Theme& light) {
    for (std::size_t k = 0; k < theme.trace.size(); ++k)
        if (theme.trace[k] == c) c = light.trace[k];
    if (c == theme.fg) c = light.fg;
    if (c == theme.dim) c = light.dim;
    if (c == theme.accent) c = light.accent;
    return svg::hex(c.r, c.g, c.b);
}

svg::Dash dash_of(LineStyle s) {
    switch (s) {
    case LineStyle::dashed: return svg::Dash::dashed;
    case LineStyle::dotted: return svg::Dash::dotted;
    case LineStyle::solid: break;
    }
    return svg::Dash::solid;
}

std::string name_at(const std::vector<std::string>& names, std::size_t k, std::size_t count) {
    if (k < names.size() && !names[k].empty()) return names[k];
    return count == 1 ? "y" : "y" + std::to_string(k + 1);
}

std::filesystem::path with_svg(std::string text) {
    std::filesystem::path p{expand_path(std::move(text))};
    if (p.extension().empty()) p += ".svg";
    return p;
}

void ask(UiContext& ui, const std::string& default_name, std::function<std::pair<std::string, std::string>()> render) {
    if (!ui.ask_text) return;
    ui.ask_text(tr("tui.export.prompt"), default_name + ".svg", [&ui, render = std::move(render)](std::string answer) {
        if (answer.empty()) return;
        const auto path = with_svg(std::move(answer));
        const auto [svg, csv] = render();
        auto csv_path = path;
        csv_path.replace_extension(".csv");
        auto r = io::write_file_atomic(path, svg);
        if (r) r = io::write_file_atomic(csv_path, csv);
        if (!r) {
            if (ui.warn) ui.warn(r.error().message);
            if (ui.notice) ui.notice(r.error().message);
            return;
        }
        if (ui.notice) ui.notice(fmt::format(fmt::runtime(tr("tui.export.saved")), fmt::arg("svg", path.string()), fmt::arg("csv", csv_path.filename().string())));
    });
}

}  // namespace

std::string export_stem(std::string_view title) {
    std::string out;
    bool gap = false;
    for (unsigned char c : title) {
        const bool keep = std::isalnum(c) || c >= 0x80 || c == '-' || c == '_' || c == '.';
        if (!keep) {
            gap = !out.empty();
            continue;
        }
        if (gap) out += '_';
        gap = false;
        out += static_cast<char>(c);
    }
    return out.empty() ? std::string{"plot"} : out;
}

svg::Chart to_chart(const PlotExport& p, const Theme& theme) {
    const auto light = builtin_theme("light");
    svg::Chart c;
    c.title = p.title;
    c.x = axis_of(p.spec.x, p.x_label);
    c.y = axis_of(p.spec.y, p.y_label);
    const auto count = p.spec.traces.size() + p.spec.envelopes.size();
    std::size_t k = 0;
    for (const auto& e : p.spec.envelopes) {
        const int wanted = e.samples > 0 ? static_cast<int>(std::min<std::size_t>(e.samples, kEnvelopeColumns)) : kEnvelopeColumns;
        auto columns = e.provider ? e.provider(wanted) : e.columns;
        svg::Series s{name_at(p.names, k++, count), colour(e.color, theme, light)};
        const bool exact = std::all_of(columns.begin(), columns.end(), [](const auto& r) { return r.lo == r.hi; });
        for (const auto& r : columns) {
            if (exact) {
                s.y.push_back(r.lo);
                continue;
            }
            s.lo.push_back(r.lo);
            s.hi.push_back(r.hi);
        }
        if (exact && columns.size() > 1 && e.samples > 0) {
            const double step = (p.spec.x.hi - p.spec.x.lo) / static_cast<double>(columns.size());
            for (std::size_t j = 0; j < columns.size(); ++j) s.x.push_back(p.spec.x.lo + step * static_cast<double>(j));
        }
        c.series.push_back(std::move(s));
    }
    for (const auto& t : p.spec.traces) {
        svg::Series s{name_at(p.names, k++, count), colour(t.color, theme, light), t.x, t.y};
        s.dash = dash_of(t.style);
        s.stems = t.stems;
        c.series.push_back(std::move(s));
    }
    for (const auto& v : {p.spec.cursor_a, p.spec.cursor_b, p.spec.playhead})
        if (v) c.x_marks.push_back(*v);
    for (double m : p.spec.x_marks) c.x_marks.push_back(m);
    c.y_marks = p.spec.y_marks;
    return c;
}

svg::Heatmap to_heatmap(const HeatmapExport& h, const Theme& theme) {
    const auto light = builtin_theme("light");
    (void)theme;
    svg::Heatmap m;
    m.title = h.title;
    m.rows = h.spec.rows;
    m.cols = h.spec.cols;
    m.values = h.spec.values;
    m.floor = h.spec.floor;
    m.ceil = h.spec.ceil;
    m.x = axis_of(h.x, h.x_label);
    m.y = axis_of(h.y, h.y_label);
    const double floor = h.spec.floor, ceil = h.spec.ceil;
    if (h.spec.scale == HeatScale::diverging) {
        double limit = 0;
        for (double v : h.spec.values)
            if (std::isfinite(v)) limit = std::max(limit, std::abs(v));
        if (!(limit > 0)) limit = 1;
        m.color = [light, limit](double v) {
            const auto c = diverging_color(v, limit, light);
            return svg::hex(c.r, c.g, c.b);
        };
    } else {
        m.color = [floor, ceil](double v) {
            const auto c = viridis(ceil > floor ? (v - floor) / (ceil - floor) : 0.0);
            return svg::hex(c.r, c.g, c.b);
        };
    }
    return m;
}

void export_plot(UiContext& ui, PlotExport plot) {
    const auto chart = to_chart(plot, ui.theme);
    ask(ui, export_stem(plot.title.empty() ? plot.id : plot.title), [chart] { return std::pair{svg::to_svg(chart), svg::to_csv(chart)}; });
}

void export_heatmap(UiContext& ui, HeatmapExport map) {
    const auto m = to_heatmap(map, ui.theme);
    ask(ui, export_stem(map.title.empty() ? map.id : map.title), [m] { return std::pair{svg::to_svg(m), svg::to_csv(m)}; });
}

MenuItem export_item(UiContext& ui, PlotExport plot) {
    return {tr("tui.menu.export_plot"), "", [&ui, plot = std::move(plot)] { export_plot(ui, plot); }};
}

MenuItem export_item(UiContext& ui, HeatmapExport map) {
    return {tr("tui.menu.export_plot"), "", [&ui, map = std::move(map)] { export_heatmap(ui, map); }};
}

}  // namespace signals::tui
