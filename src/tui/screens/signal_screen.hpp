#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/signal_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class SignalScreen final : public TabScreen {
public:
    SignalScreen(UiContext& ui, std::shared_ptr<SignalModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;

    ftxui::Box area(std::string_view id) const;
    Axis plot_axis(std::string_view id) const;
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::optional<double> playhead() const;

private:
    enum class DragMode { pan, select };
    enum Panel { wave, spectrum, bands, source, progress, log, panel_count };

    struct Drag {
        enum Kind { none, pan, select, divider } kind = none;
        int last_x = 0;
        double t0 = 0;
        bool moved = false;
        bool shift = false;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element source_panel(const SignalData* d);
    ftxui::Element run_panel(int panel_width);
    ftxui::Element log_panel(int rows);
    ftxui::Element wave_panel(const std::shared_ptr<const SignalData>& d, int rows, int width);
    ftxui::Element spectrum_panel(const SignalData* d, int rows, int width);
    ftxui::Element bands_panel(const SignalData* d, int width);
    ftxui::Element build(Panel p, const std::shared_ptr<const SignalData>& d, int rows, int width);

    bool on_mouse(const ftxui::Mouse& m);
    bool in_wave(int x, int y) const;
    double time_at(int x) const;
    int wave_columns() const;
    bool loading() const;
    bool playing() const;
    void toggle_playback();
    static Panel panel_of(const std::string& key);
    static std::string key_of(Panel p);

    UiContext& ui_;
    std::shared_ptr<SignalModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = wave;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::array<ftxui::Box, panel_count> title_boxes_{};
    std::array<ftxui::Box, 2> wave_boxes_{};
    ftxui::Box divider_box_{};
    ftxui::Box root_box_{};
    bool wave_labels_ = true;
    bool wave_split_ = true;
    Drag drag_;
    PlotZoom zoom_;
    DragMode drag_mode_ = DragMode::pan;
    ftxui::Box drag_chip_{};
    bool audio_warned_ = false;
    bool arrows_move_b_ = false;
    Panel last_title_ = panel_count;
    std::chrono::steady_clock::time_point last_title_click_{};
};

std::shared_ptr<TabScreen> make_signal_screen(UiContext& ui, std::shared_ptr<SignalModel> model);

}  // namespace signals::tui
