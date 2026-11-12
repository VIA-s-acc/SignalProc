#pragma once

#include <string>
#include <vector>

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

struct HeatmapExport {
    std::string id, title;
    HeatmapSpec spec;
    Axis x = {}, y = {};
    std::string x_label = {}, y_label = {};
};

}  // namespace signals::tui
