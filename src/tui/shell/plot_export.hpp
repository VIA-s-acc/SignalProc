#pragma once

#include <map>
#include <string>
#include <string_view>

#include "svg/svg.hpp"
#include "tui/shell/screen.hpp"
#include "tui/theme/theme.hpp"
#include "tui/widgets/zplane.hpp"

namespace signals::tui {

svg::Chart to_chart(const PlotExport& plot, const Theme& theme);
svg::Heatmap to_heatmap(const HeatmapExport& map, const Theme& theme);
svg::Chart to_chart(const ChartExport& chart, const Theme& theme);
ChartExport zplane_export(std::string id, std::string title, const ZPlaneSpec& spec, const Theme& theme,
                          const std::map<std::string, std::string, std::less<>>& marker_names = {});
std::string export_stem(std::string_view title);
void export_plot(UiContext& ui, PlotExport plot);
void export_heatmap(UiContext& ui, HeatmapExport map);
void export_chart(UiContext& ui, ChartExport chart);
MenuItem export_item(UiContext& ui, PlotExport plot);
MenuItem export_item(UiContext& ui, HeatmapExport map);
MenuItem export_item(UiContext& ui, ChartExport chart);

}  // namespace signals::tui
