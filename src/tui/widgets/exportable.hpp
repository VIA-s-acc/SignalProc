#pragma once

#include <string>
#include <vector>

#include "svg/svg.hpp"
#include "tui/widgets/heatmap.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {

struct PlotMeta {
    std::string title = {};
    std::vector<std::string> names = {};
    std::string x_label = {}, y_label = {};
};

struct PlotExport {
    std::string id, title;
    PlotSpec spec;
    std::vector<std::string> names = {};
    std::string x_label = {}, y_label = {};
};

struct ChartExport {
    std::string id, title;
    svg::Chart chart;
    std::vector<Rgb> colors = {};
    std::string csv = {};
    int width = 960, height = 420;
};

struct HeatmapExport {
    std::string id, title;
    HeatmapSpec spec;
    Axis x = {}, y = {};
    std::string x_label = {}, y_label = {};
};

}  // namespace signals::tui
