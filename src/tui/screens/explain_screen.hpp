#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "core/bank.hpp"
#include "tui/screens/explain_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class ExplainScreen final : public TabScreen {
public:
    ExplainScreen(UiContext& ui, std::function<std::optional<core::FilterBank>()> user_bank);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    void open_with(const NavArgs& args) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::vector<Command> commands() override;

    std::size_t step() const { return step_; }
    ExplainOptions options() const { return options_; }
    ftxui::Box area(std::string_view id) const;
    Axis plot_axis(std::string_view id) const;
    std::vector<MenuItem> context_menu(int x, int y) override;

private:
    enum Panel { scheme, steps, story, matrix, freq, p0, panel_count };

    const Walkthrough* current();
    void set_step(std::size_t step);
    void set_bank(ExplainBank bank);
    void cycle_bank();
    std::string wavelet_name() const;
    std::chrono::steady_clock::time_point now() const;

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element scheme_panel(const Walkthrough& w, int width, bool with_steps);
    ftxui::Element steps_body();
    ftxui::Element text_panel(const Walkthrough& w);
    ftxui::Element matrix_panel(const Walkthrough& w);
    ftxui::Element freq_panel(const Walkthrough& w);
    ftxui::Element p0_panel(const Walkthrough& w);
    ftxui::Element rows_table(const Walkthrough& w, int width);

    UiContext& ui_;
    std::function<std::optional<core::FilterBank>()> user_bank_;
    ExplainOptions options_;
    std::optional<Walkthrough> walkthrough_;
    std::optional<std::string> error_;
    bool dirty_ = true;
    std::size_t step_ = 1;
    bool auto_ = false;
    std::chrono::steady_clock::time_point last_advance_{};
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = scheme;
    std::optional<Panel> maximized_;
    PlotZoom zoom_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::array<ftxui::Box, 12> step_boxes_{};
    std::array<ftxui::Box, 4> bank_boxes_{};
    ftxui::Box prev_box_{}, next_box_{}, auto_box_{};
};

std::shared_ptr<TabScreen> make_explain_screen(UiContext& ui, std::function<std::optional<core::FilterBank>()> user_bank);

}  // namespace signals::tui
