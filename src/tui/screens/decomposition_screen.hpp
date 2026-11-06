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

namespace signals::tui {

class DecompositionScreen final : public TabScreen {
public:
    enum class View { subbands, mra, tiles };

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
    std::vector<Command> commands() override;

    ftxui::Box area(std::string_view id) const;
    View view() const { return view_; }
    std::size_t selected_band() const { return selected_; }
    std::optional<double> cursor() const { return cursor_; }
    void set_cursor(double t);
    std::pair<double, double> time_view() const { return {begin_, end_}; }

private:
    enum Panel { view_panel, energy, cursor_panel, denoise, listen, summary, panel_count };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element toolbar(bool two_rows);
    ftxui::Element view_body(const DecompositionData& d, const Reconstruction* r, int width, int max_rows);
    ftxui::Element more_row(bool up, std::size_t count) const;
    ftxui::Element time_axis(int width) const;
    ftxui::Element energy_panel(const DecompositionData& d, int max_rows, int bar_width);
    ftxui::Element cursor_body(const DecompositionData& d, const Reconstruction* r);
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
    std::vector<ftxui::Box> plot_boxes_;
    std::shared_ptr<const DecompositionData> seen_;
    double begin_ = 0, end_ = 0;
    std::optional<double> cursor_;
    std::size_t selected_ = 0;
    std::size_t band_first_ = 0, energy_first_ = 0;
    Processing draft_;
    std::string notice_;
    struct Drag {
        bool active = false, moved = false;
        int last_x = 0;
    } drag_;
};

std::shared_ptr<TabScreen> make_decomposition_screen(UiContext& ui, std::shared_ptr<DecompositionModel> model);

}  // namespace signals::tui
