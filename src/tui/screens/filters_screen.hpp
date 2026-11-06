#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/filters_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {

class FiltersScreen final : public TabScreen {
public:
    static constexpr std::size_t kPoints = 4096;

    FiltersScreen(UiContext& ui, std::shared_ptr<ComparisonSet> set);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    bool capturing_text() const override { return searching_; }

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;

    ftxui::Box area(std::string_view id) const;
    std::pair<double, double> view() const { return {lo_, hi_}; }
    std::optional<double> cursor_a() const { return cursor_a_; }
    std::optional<double> cursor_b() const { return cursor_b_; }
    void set_cursor_a(double w) { cursor_a_ = std::clamp(w, 0.0, 1.0); }
    double omega_at(std::string_view plot, int x) const;
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::vector<JumpTarget> jump_targets() const override;

private:
    enum Panel { magnitude, phase, delay, impulse, zeros, properties, measures, library, comparison, panel_count };
    enum class Plot { magnitude, phase, delay };

    struct Clickable {
        ftxui::Box box;
        std::function<void()> action;
    };

    struct Drag {
        bool active = false;
        bool moved = false;
        bool shift = false;
        int plot = 0;
        int last_x = 0;
    };

    struct Trace {
        const ComparisonSet::Entry* entry;
        Rgb color;
        LineStyle style;
        const core::FrequencyResponse* response;
        double peak_db;
        double peak;
        bool normalised;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element build(Panel p, int rows, int width);
    ftxui::Element magnitude_panel(int width);
    ftxui::Element phase_panel();
    ftxui::Element delay_panel();
    ftxui::Element impulse_panel(int width);
    ftxui::Element zeros_panel(int rows);
    ftxui::Element properties_panel();
    ftxui::Element measures_panel();
    ftxui::Element library_panel(int width);
    ftxui::Element comparison_panel();
    ftxui::Element clickable(ftxui::Element e, std::function<void()> action);
    struct Item {
        std::string label;
        std::function<void()> action;
        bool on = false;
    };
    ftxui::Element chip_rows(std::vector<Item> items, int width, int indent);

    Axis omega_axis() const;
    double to_axis(double w) const;
    double from_axis(double v) const;
    PlotSpec frequency_spec() const;
    std::vector<Trace> traces() const;
    Rgb color_of(std::size_t index) const;
    LineStyle style_of(std::size_t index) const;
    std::string legend_mark(std::size_t index) const;
    const ComparisonSet::Entry* focused() const;
    std::optional<double> value_at(const Trace& t, double w) const;
    std::optional<std::size_t> index_at(const core::FrequencyResponse& r, double w) const;
    ftxui::Element cursor_line(const std::vector<Trace>& ts, const std::function<double(const Trace&, std::size_t)>& value,
                               const std::string& unit) const;

    void add(const FilterRef& ref);
    void zoom(double factor, double anchor);
    void pan(double delta);
    std::vector<const LibraryNode*> matches() const;
    bool on_mouse(const ftxui::Mouse& m);
    bool on_search(ftxui::Event e);
    int plot_under(int x, int y) const;
    ftxui::Box plot_box(int plot) const;
    static Panel panel_of(const std::string& key);
    static std::string key_of(Panel p);

    UiContext& ui_;
    std::shared_ptr<ComparisonSet> set_;
    LibraryNode library_;
    std::vector<ComparisonSet::Entry> entries_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = magnitude;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::array<ftxui::Box, panel_count> title_boxes_{};
    std::array<ftxui::Box, 3> plot_boxes_{};
    std::deque<Clickable> clickables_;
    std::vector<ftxui::Box> row_boxes_;
    double lo_ = 0, hi_ = 1;
    std::optional<double> cursor_a_, cursor_b_;
    double db_floor_ = -80;
    double phase_zoom_ = 1, delay_zoom_ = 1, linear_zoom_ = 1;
    bool log_ = false, decibels_ = true, normalise_ = true;
    FilterRole add_role_ = FilterRole::h0;
    std::string family_ = "db";
    std::vector<std::string> collapsed_;
    std::string notice_;
    bool searching_ = false;
    std::string query_;
    std::size_t match_index_ = 0;
    Drag drag_;
};

std::shared_ptr<TabScreen> make_filters_screen(UiContext& ui, std::shared_ptr<ComparisonSet> set);

}  // namespace signals::tui
