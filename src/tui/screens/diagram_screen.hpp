#pragma once

#include <deque>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/screens/designer_model.hpp"
#include "tui/screens/diagram_model.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/plot_zoom.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/bank_diagram.hpp"

namespace signals::tui {

class DiagramScreen final : public TabScreen {
public:
    enum class ProbeView { time, spectrum, numbers };

    enum Panel { diagram_panel_id, probe_panel_id, panel_count };

    DiagramScreen(UiContext& ui, std::shared_ptr<DesignerModel> designer, std::shared_ptr<SignalModel> signal);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::optional<TheoryLink> theory_link() const override;
    void publish(theory::LiveValues& v) const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;

    std::string probe() const { return probe_; }
    bool polyphase() const { return polyphase_; }
    ProbeView probe_view() const { return view_; }
    std::size_t length() const;
    std::string bank_name() const { return bank_name_; }
    std::shared_ptr<const DiagramResult> result() const { return result_; }
    ftxui::Box area(std::string_view id) const;
    std::vector<MenuItem> context_menu(int x, int y) override;
    Panel focus() const { return focus_; }
    std::optional<Axis> plot_view(std::string_view id) const { return zoom_.view(id); }

private:
    struct Clickable {
        ftxui::Box box;
        std::function<void()> action;
    };
    struct Item {
        std::string label;
        std::function<void()> action;
        bool on = false;
    };

    void refresh();
    ftxui::Element clickable(ftxui::Element e, std::function<void()> action);
    ftxui::Element chips(std::vector<Item> items);
    ftxui::Element diagram_panel(int width);
    ftxui::Element probe_panel(int width, int height);
    void select(const std::string& id);
    std::vector<std::string> wires() const;

    UiContext& ui_;
    std::shared_ptr<DesignerModel> designer_;
    std::shared_ptr<SignalModel> signal_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    std::size_t bank_ = 0, length_ = 1, bank_length_ = 0;
    bool file_ = false, polyphase_ = false;
    ProbeView view_ = ProbeView::time;
    Panel focus_ = diagram_panel_id;
    std::optional<Panel> maximized_;
    PanelLayout layout_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    PlotZoom zoom_;
    std::string probe_ = "a", hover_, bank_name_, notice_, key_;
    std::shared_ptr<const DiagramResult> result_;
    std::shared_ptr<const DesignerResult> source_bank_;
    std::shared_ptr<const SignalData> source_data_, peak_data_;
    std::size_t peak_start_ = 0, peak_length_ = 0;
    std::shared_ptr<const DiagramResult> spectrum_of_;
    std::string spectrum_wire_;
    bool spectrum_polyphase_ = false;
    std::vector<double> spectrum_db_;
    DiagramHits hits_;
    std::deque<Clickable> clickables_;
};

std::shared_ptr<TabScreen> make_diagram_screen(UiContext& ui, std::shared_ptr<DesignerModel> designer, std::shared_ptr<SignalModel> signal);

}  // namespace signals::tui
