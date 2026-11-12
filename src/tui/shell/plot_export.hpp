#pragma once

#include <string>
#include <string_view>

#include "svg/svg.hpp"
#include "tui/shell/screen.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

svg::Chart to_chart(const PlotExport& plot, const Theme& theme);
svg::Heatmap to_heatmap(const HeatmapExport& map, const Theme& theme);
std::string export_stem(std::string_view title);
void export_plot(UiContext& ui, PlotExport plot);
void export_heatmap(UiContext& ui, HeatmapExport map);
MenuItem export_item(UiContext& ui, PlotExport plot);
MenuItem export_item(UiContext& ui, HeatmapExport map);

}  // namespace signals::tui
