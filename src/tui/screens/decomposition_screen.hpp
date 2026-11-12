#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/decomposition_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {

class DecompositionScreen final : public TabScreen {
public:
    enum class View { subbands, mra, tiles, packets, scalogram, sst };

    DecompositionScreen(UiContext& ui, std::shared_ptr<DecompositionModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::optional<std::pair<int, int>> menu_anchor() const override;
    void open_with(const NavArgs& args) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::optional<std::string> explain_at(int x, int y) const override;
    std::vector<Command> commands() override;

    ftxui::Box area(std::string_view id) const;
    View view() const { return view_; }
    void set_view(View v) { view_ = v; }
    std::optional<int> packet_row(std::size_t level) const;
    core::PacketNode packet_selection() const { return {pk_level_, core::natural_index_at(pk_level_, pk_pos_)}; }
    std::size_t selected_band() const { return selected_; }
    std::optional<double> cursor() const { return cursor_; }
    void set_cursor(double t);
    std::pair<double, double> time_view() const { return {begin_, end_}; }
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::vector<HeatmapExport> heatmap_exports() override;
    std::vector<PlotExport> plot_exports() override { return shown_plots_; }
    std::vector<ChartExport> chart_exports() override;

private:
    enum Panel { view_panel, energy, cursor_panel, denoise, listen, summary, panel_count };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element toolbar(bool two_rows, int cols);
    ftxui::Element view_body(std::shared_ptr<const DecompositionData> data, std::shared_ptr<const Reconstruction> recon, int width, int max_rows);
    ftxui::Element more_row(bool up, std::size_t count) const;
    ftxui::Element time_axis(int width) const;
    ftxui::Element energy_panel(const DecompositionData& d, int max_rows, int width, bool wide);
    ftxui::Element packets_body(const PacketData& p, int width, int max_rows);
    ftxui::Element scalogram_body(const ScalogramData& s, int width, int max_rows);
    ftxui::Element packets_energy(const PacketData& p, int max_rows, int width);
    void pick_packet(core::PacketNode n);
    bool packet_key(const ftxui::Event& e, const PacketData& p);
    ftxui::Element cursor_body(const DecompositionData& d, const Reconstruction* r, int max_rows);
    void select(std::size_t band);
    ftxui::Element denoise_panel(const DecompositionData& d, const Reconstruction* r);
    ftxui::Element listen_panel(const DecompositionData& d);
    ftxui::Element summary_panel(const DecompositionData& d, const Reconstruction* r);
    void sync_view(const std::shared_ptr<const DecompositionData>& d);
    void zoom(double factor, double anchor);
    void pan(double dt);
    double time_at(int x) const;
    ftxui::Box plot_area() const;
    std::optional<double> playhead() const;
    void play(ClipKind kind);
    void set_transform(Transform t, JobRunner& jobs);
    std::string band_label(std::size_t i) const;

    UiContext& ui_;
    std::shared_ptr<DecompositionModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = view_panel;
    std::optional<Panel> maximized_;
    View view_ = View::subbands;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::deque<Clickable> clickables_;
    Notes notes_{"decomposition"};
    std::optional<HeatmapExport> shown_map_;
    std::vector<PlotExport> shown_plots_;
    std::optional<ChartExport> shown_chart_;
    std::vector<ftxui::Box> plot_boxes_;
    std::shared_ptr<const DecompositionData> seen_;
    double begin_ = 0, end_ = 0;
    std::optional<double> cursor_;
    std::size_t selected_ = 0;
    std::size_t band_first_ = 0, energy_first_ = 0, cursor_first_ = 0;
    bool follow_ = true;
    std::vector<ftxui::Box> label_boxes_;
    std::vector<ftxui::Box> packet_boxes_;
    std::vector<std::size_t> packet_levels_;
    std::size_t packet_first_ = 0, pk_level_ = 0, pk_pos_ = 0;
    Processing draft_;
    std::string notice_;
    struct Drag {
        bool active = false, moved = false;
        int last_x = 0;
    } drag_;
};

std::shared_ptr<TabScreen> make_decomposition_screen(UiContext& ui, std::shared_ptr<DecompositionModel> model);

}  // namespace signals::tui
