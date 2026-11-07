#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/matrix_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/axis_zoom.hpp"

namespace signals::tui {

class MatrixScreen final : public TabScreen {
public:
    MatrixScreen(UiContext& ui, std::shared_ptr<MatrixModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    void open_with(const NavArgs& args) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::vector<Command> commands() override;

    struct Block {
        std::size_t r0 = 0, rows = 0, c0 = 0, cols = 0;
    };

    std::size_t row() const { return row_; }
    std::size_t column() const { return col_; }
    Block block() const { return block_; }
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::optional<std::pair<int, int>> menu_anchor() const override;
    bool numbers() const { return numbers_; }
    ftxui::Box area(std::string_view id) const;
    std::optional<Axis> plot_view(std::string_view id) const { return sigma_zoom_.view(id); }

private:
    enum Panel { controls, checks, main, origin, panel_count };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    void show_cell();
    ftxui::Element controls_panel();
    ftxui::Element checks_panel(const MatrixData* d);
    ftxui::Element main_panel(const MatrixData* d, int width, int height);
    ftxui::Element row_info(const MatrixData& d);
    ftxui::Element origin_panel(const MatrixData* d, int width);
    void apply(MatrixOptions o);
    void update_block(std::size_t rows, std::size_t cols);
    std::pair<double, double> fractions_at(int x, int y) const;
    int max_numbers_n(int width) const;
    std::chrono::steady_clock::time_point now() const;

    UiContext& ui_;
    std::shared_ptr<MatrixModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = main;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::deque<Clickable> clickables_;
    ftxui::Box matrix_box_{};
    std::size_t row_ = 5, col_ = 0;
    bool numbers_ = false;
    bool spectrum_ = false;
    bool animate_ = false;
    std::chrono::steady_clock::time_point last_step_{};
    int main_width_ = 100;
    std::string notice_;
    std::optional<Panel> maximized_;
    AxisZoom row_zoom_, col_zoom_;
    PlotZoom sigma_zoom_;
    double origin_scroll_ = 0;
    Block block_;
    bool drag_ = false;
    int drag_x_ = 0, drag_y_ = 0;
};

std::shared_ptr<TabScreen> make_matrix_screen(UiContext& ui, std::shared_ptr<MatrixModel> model);

}  // namespace signals::tui
