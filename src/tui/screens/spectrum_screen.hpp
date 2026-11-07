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

#include "tui/screens/spectrum_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class SpectrumScreen final : public TabScreen {
public:
    struct Measure {
        double t0 = 0, t1 = 0, f0 = 0, f1 = 0;
    };

    SpectrumScreen(UiContext& ui, std::shared_ptr<SpectrumModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::optional<std::pair<int, int>> menu_anchor() const override;

    ftxui::Box area(std::string_view id) const;
    std::optional<Axis> plot_view(std::string_view id) const { return zoom_.view(id); }
    std::optional<std::pair<double, double>> cursor() const { return cursor_; }
    std::optional<Measure> measurement() const { return measure_; }
    bool harmonics_shown() const { return harmonics_; }
    std::size_t fold() const { return fold_; }
    bool log_axis() const { return log_; }
    double floor_db() const { return floor_db_; }
    std::pair<double, double> time_view() const { return {begin_, end_}; }

private:
    enum Panel { spectrogram_panel, spectrum_panel, cursor_panel, window_panel, measure_panel, list_panel, theory_panel, panel_count };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element toolbar(bool two_rows);
    ftxui::Element spectrogram_body(const SpectrogramData& s, const SelectionSpectrum* sel, int width, int height);
    ftxui::Element spectrum_body(const SelectionSpectrum& sel, int width, int height);
    ftxui::Element cursor_panel_body(const SpectrogramData* s, const SelectionSpectrum* sel);
    ftxui::Element window_body(const SpectrogramData* s);
    ftxui::Element measure_body(const SpectrogramData* s);
    ftxui::Element list_body(const SelectionSpectrum* sel, int rows);
    ftxui::Element time_axis(int width, std::optional<std::pair<double, double>> selection) const;
    void sync_view(const std::shared_ptr<const SpectrogramData>& s);
    void zoom(double factor, double anchor);
    void pan(double dt);
    double time_at(int x) const;
    double freq_at(int y) const;
    double nyquist() const;
    bool log_view() const;
    void set_n(std::size_t n);

    UiContext& ui_;
    std::shared_ptr<SpectrumModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = spectrogram_panel;
    PlotZoom zoom_;
    std::optional<Panel> maximized_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::deque<Clickable> clickables_;
    ftxui::Box plot_box_{};
    std::shared_ptr<const SpectrogramData> seen_;
    std::shared_ptr<const SpectrogramData> grid_for_;
    GridSpec grid_spec_;
    std::vector<double> grid_;
    double begin_ = 0, end_ = 0;
    std::optional<std::pair<double, double>> cursor_;
    std::optional<Measure> measure_;
    bool harmonics_ = true, log_ = true, grey_ = false;
    std::size_t fold_ = 0;
    double floor_db_ = -90;
    struct Drag {
        bool active = false, moved = false, select = false;
        int x = 0, y = 0;
    } drag_;
};

std::shared_ptr<TabScreen> make_spectrum_screen(UiContext& ui, std::shared_ptr<SpectrumModel> model);

}  // namespace signals::tui
