#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/pipeline_canvas.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class PipelineScreen final : public TabScreen {
public:
    PipelineScreen(UiContext& ui, std::shared_ptr<PipelineModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    bool capturing_text() const override { return adding_; }

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;

    ftxui::Box area(std::string_view id) const;
    const std::string& notice() const { return notice_; }
    std::pair<int, int> view_offset() const { return {ox_, oy_}; }

private:
    enum Panel { kGraph, kInspector, kRun, kText, panel_count };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element toolbar() const;
    ftxui::Element graph_panel(int width, int height);
    ftxui::Element inspector_panel();
    ftxui::Element add_panel();
    bool on_add_key(const ftxui::Event& e);
    bool on_inspector_key(const ftxui::Event& e);
    void edit_param(int step);
    void report(const core::Result<void>& r);
    const pipeline::GraphNode* selected_node() const;
    ftxui::Element run_panel();
    ftxui::Element text_panel(int height);
    void follow_selection();
    void clamp_view();
    void move_selection(int dx, int dy);
    void cycle_wires();
    bool on_mouse(const ftxui::Mouse& m);
    std::optional<std::pair<int, int>> to_picture(int x, int y) const;

    UiContext& ui_;
    std::shared_ptr<PipelineModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = kGraph;
    std::optional<Panel> maximized_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    ftxui::Box graph_box_{};
    GraphPicture picture_;
    int ox_ = 0, oy_ = 0, view_w_ = 0, view_h_ = 0;
    bool follow_ = false;
    std::string anchor_;
    struct Drag {
        enum class Kind { none, wire, pan } kind = Kind::none;
        PortHit from;
        int last_x = 0, last_y = 0;
    } drag_;
    std::string notice_;
    bool adding_ = false;
    std::string query_;
    std::size_t pick_ = 0, param_ = 0;
};

std::shared_ptr<TabScreen> make_pipeline_screen(UiContext& ui, std::shared_ptr<PipelineModel> model);

}  // namespace signals::tui
