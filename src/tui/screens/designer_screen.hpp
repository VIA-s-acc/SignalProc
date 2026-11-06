#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/designer_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/widgets/zplane.hpp"

namespace signals::tui {

class DesignerScreen final : public TabScreen {
public:
    DesignerScreen(UiContext& ui, std::shared_ptr<DesignerModel> model);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::vector<JumpTarget> jump_targets() const override;
    std::vector<Command> commands() override;
    std::vector<MenuItem> context_menu(int x, int y) override;
    std::optional<std::pair<int, int>> menu_anchor() const override;

    ftxui::Box area(std::string_view id) const;
    Axis plot_axis(std::string_view id) const;
    std::vector<ZHit> root_hits() const { return hits_ ? *hits_ : std::vector<ZHit>{}; }

private:
    enum Panel { roots, splits, result, magnitude, phi, phi_dual, panel_count };

    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element roots_panel(const DesignerData* d, const DesignerResult* r);
    ftxui::Element splits_panel(const DesignerData* d);
    ftxui::Element result_panel(const DesignerResult* r, int width);
    ftxui::Element magnitude_panel(const DesignerResult* r);
    ftxui::Element cascade_panel(const DesignerResult* r, bool dual);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    void save();
    double zplane_extent(const DesignerData* d) const;
    JobRunner* jobs() const { return ui_.jobs; }

    UiContext& ui_;
    std::shared_ptr<DesignerModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = roots;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::deque<Clickable> clickables_;
    std::shared_ptr<std::vector<ZHit>> hits_ = std::make_shared<std::vector<ZHit>>();
    ftxui::Box roots_box_{};
    std::string notice_;
    std::optional<Panel> maximized_;
    std::size_t group_cursor_ = 0, preset_index_ = 0;
    PlotZoom zoom_;
    double zplane_zoom_ = 1;
};

std::shared_ptr<TabScreen> make_designer_screen(UiContext& ui, std::shared_ptr<DesignerModel> model);

}  // namespace signals::tui
