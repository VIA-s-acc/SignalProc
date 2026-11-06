#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/data/pyramid.hpp"
#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

enum class LineStyle { solid, dashed, dotted };
enum class AxisScale { linear, log10, pi };

struct Axis {
    double lo = 0, hi = 1;
    AxisScale scale = AxisScale::linear;
};

struct Trace {
    std::vector<double> y = {};
    std::vector<double> x = {};
    Rgb color = {};
    LineStyle style = LineStyle::solid;
    bool focused = false;
    bool stems = false;
};

struct Envelope {
    std::vector<MinMaxPyramid::Range> columns = {};
    Rgb color = {};
    std::function<std::vector<MinMaxPyramid::Range>(int dot_columns)> provider = {};
};

struct PlotSpec {
    Axis x = {}, y = {};
    std::vector<Trace> traces = {};
    std::vector<Envelope> envelopes = {};
    std::optional<double> cursor_a = {}, cursor_b = {};
    std::optional<double> playhead = {};
    std::optional<std::pair<double, double>> selection = {};
    std::vector<double> x_marks = {};
    std::vector<double> y_marks = {};
    bool labels = true;
    bool x_labels = true;
};

ftxui::Element plot(PlotSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs);

ftxui::Box plot_area(const ftxui::Box& outer, const PlotSpec& spec);

double axis_fraction(const Axis& a, double v);
double value_at_column(const Axis& a, int column, int columns);
int column_of_value(const Axis& a, double v, int columns);

}  // namespace signals::tui
