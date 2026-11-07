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

#include "tui/screens/signal_model.hpp"
#include "tui/screens/tempo_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {

class TempoScreen final : public TabScreen {
public:
    enum Panel { controls_panel, before_panel, after_panel, readouts_panel, wave_panel, panel_count };

    TempoScreen(UiContext& ui, std::shared_ptr<SignalModel> signal);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::optional<std::string> explain_at(int x, int y) const override;

    Panel focus() const { return focus_; }
    ftxui::Box area(std::string_view id) const;
    std::optional<Axis> plot_view(std::string_view id) const { return zoom_.view(id); }
    const TempoOptions& options() const { return options_; }
    std::shared_ptr<const TempoView> view() const { return view_; }
    const std::string& notice() const { return notice_; }

private:
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    void refresh();
    void play(bool processed);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element chip(std::string id, const std::string& label, bool on, std::function<void()> action);
    ftxui::Element frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body);
    ftxui::Element render_panel(Panel p, int width);
    ftxui::Element controls(int width);
    ftxui::Element track_body(bool after);
    ftxui::Element readouts_body(int width);
    ftxui::Element wave_body();
    Axis pitch_axis() const;

    UiContext& ui_;
    std::shared_ptr<SignalModel> signal_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    TempoOptions options_;
    std::optional<TempoOptions> computed_;
    std::shared_ptr<const SignalData> computed_data_;
    std::optional<std::pair<double, double>> computed_selection_;
    std::shared_ptr<const TempoView> view_;
    std::string notice_;
    std::deque<Clickable> clickables_;
    Notes notes_{"tempo"};
    PanelLayout layout_;
    Panel focus_ = controls_panel;
    std::optional<Panel> maximized_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    PlotZoom zoom_;
    std::vector<Panel> small_panels_;
};

std::shared_ptr<TabScreen> make_tempo_screen(UiContext& ui, std::shared_ptr<SignalModel> signal);

}  // namespace signals::tui
