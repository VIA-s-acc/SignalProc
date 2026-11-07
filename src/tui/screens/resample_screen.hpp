#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/resample_model.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class ResampleScreen final : public TabScreen {
public:
    enum Panel { controls_panel, before_panel, after_panel, readouts_panel, time_panel, panel_count };

    ResampleScreen(UiContext& ui, std::shared_ptr<SignalModel> signal);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::vector<MenuItem> context_menu(int x, int y) override;

    Panel focus() const { return focus_; }
    ftxui::Box area(std::string_view id) const;
    std::optional<Axis> plot_view(std::string_view id) const { return zoom_.view(id); }

    const ResampleOptions& options() const { return options_; }
    std::shared_ptr<const ResampleView> view() const { return view_; }
    const std::string& notice() const { return notice_; }
    std::size_t fragment_start() const { return start_; }

private:
    struct Clickable {
        ftxui::Box box;
        std::function<void()> action;
    };

    void refresh();
    ftxui::Element clickable(ftxui::Element e, std::function<void()> action);
    ftxui::Element chip(const std::string& label, bool on, std::function<void()> action);
    ftxui::Element frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body);
    ftxui::Element render_panel(Panel p);
    ftxui::Element controls();
    ftxui::Element before_body();
    ftxui::Element after_body();
    ftxui::Element readouts_body();
    ftxui::Element time_body();
    std::vector<Panel> shown_panels(int cols) const;

    UiContext& ui_;
    std::shared_ptr<SignalModel> signal_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    ResampleOptions options_;
    std::optional<ResampleOptions> computed_;
    std::shared_ptr<const SignalData> computed_data_, peak_data_;
    std::size_t start_ = 0, computed_start_ = 0, peak_start_ = 0;
    std::shared_ptr<const ResampleView> view_;
    std::string notice_;
    std::deque<Clickable> clickables_;
    PanelLayout layout_;
    Panel focus_ = controls_panel;
    std::optional<Panel> maximized_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    PlotZoom zoom_;
    std::vector<Panel> small_panels_;
};

std::shared_ptr<TabScreen> make_resample_screen(UiContext& ui, std::shared_ptr<SignalModel> signal);

}  // namespace signals::tui
