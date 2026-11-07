#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/data/pyramid.hpp"
#include "tui/screens/pipeline_canvas.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/scroll.hpp"

namespace signals::tui {

class PipelineScreen final : public TabScreen {
public:
    PipelineScreen(UiContext& ui, std::shared_ptr<PipelineModel> model, std::function<std::optional<std::filesystem::path>()> current_file = {});

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    bool capturing_text() const override { return adding_; }
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::vector<Command> commands() override;
    std::string context() const override;

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;

    ftxui::Box area(std::string_view id) const;
    std::optional<Axis> plot_view(std::string_view id) const;
    std::vector<MenuItem> context_menu(int x, int y) override;
    const std::string& notice() const { return notice_; }
    std::pair<int, int> view_offset() const { return {ox_, oy_}; }

private:
    enum Panel { kGraph, kProbe, kInspector, kRun, kText, panel_count };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element toolbar() const;
    ftxui::Element graph_panel(int width, int height);
    ftxui::Element inspector_panel(int width, int height);
    std::vector<std::string> param_values(const pipeline::BlockType& type, const pipeline::ParamSpec& spec) const;
    void add_block(const std::string& type);
    void open_example(std::string_view name, std::string_view text);
    ftxui::Element add_panel(int width, int height);
    bool on_add_key(const ftxui::Event& e);
    bool on_inspector_key(const ftxui::Event& e);
    void edit_param(int step);
    void report(const core::Result<void>& r);
    const pipeline::GraphNode* selected_node() const;
    ftxui::Element run_panel(int width, int height);
    ftxui::Element probe_panel(int width, int height);
    void refresh_probe(const RunView& run);
    void play(bool input);
    std::optional<double> playhead() const;
    ftxui::Element text_panel(int width, int height);
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
        ftxui::Box box{};
    } drag_;
    std::string notice_;
    std::vector<std::pair<std::string, ftxui::Box>> add_rows_;
    Scroll text_scroll_, run_scroll_, add_scroll_, inspector_scroll_;
    std::vector<ftxui::Box> param_boxes_;
    std::string param_follow_;
    PlotZoom probe_zoom_;
    std::string text_follow_;
    std::size_t add_follow_ = static_cast<std::size_t>(-1);
    std::function<std::optional<std::filesystem::path>()> current_file_;
    bool seeded_ = false;
    struct ProbeData {
        std::uint64_t run = 0;
        std::string name;
        std::shared_ptr<std::vector<std::vector<float>>> samples;
        std::unique_ptr<MinMaxPyramid> pyramid;
        std::vector<double> freqs, db;
        double rate = 0;
        std::size_t frames = 0;
    } probe_;
    std::size_t probe_index_ = 0;
    bool spectrum_ = false, last_input_ = false;
    bool adding_ = false;
    std::optional<pipeline::PortRef> connecting_;
    std::string query_;
    std::size_t pick_ = 0, param_ = 0;
};

std::shared_ptr<TabScreen> make_pipeline_screen(UiContext& ui, std::shared_ptr<PipelineModel> model,
                                                std::function<std::optional<std::filesystem::path>()> current_file = {});

}  // namespace signals::tui
