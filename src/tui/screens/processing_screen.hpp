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

#include "tui/screens/processing_model.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

class ProcessingScreen final : public TabScreen {
public:
    enum class View { a, b, x };

    ProcessingScreen(UiContext& ui, std::shared_ptr<ProcessingModel> model);

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
    View view() const { return view_; }
    std::size_t preset() const { return preset_; }

private:
    enum Panel { coeffs_panel, signal_panel, curve_panel, eq_panel, summary_panel, noise_panel, listen_panel, theory_panel, panel_count };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    ftxui::Element frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title);
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element toolbar(bool two_rows);
    ftxui::Element coeffs_body(const ProcessingResult& r, int width, int rows);
    ftxui::Element signal_body(const ProcessingResult& r, int width, int height);
    ftxui::Element curve_body(const ProcessingResult& r, int width, int height);
    ftxui::Element eq_body(const ProcessingResult& r, int width, int height);
    ftxui::Element summary_body(const ProcessingResult& r);
    ftxui::Element noise_body(const ProcessingResult& r);
    ftxui::Element listen_body();
    void change(const std::function<void(ProcessingOptions&)>& edit);
    void set_scale(double s);
    void set_preset(std::size_t p);
    void play(View v);

    UiContext& ui_;
    std::shared_ptr<ProcessingModel> model_;
    PanelLayout layout_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = coeffs_panel;
    std::optional<Panel> maximized_;
    std::array<ftxui::Box, panel_count> panel_boxes_{};
    std::deque<Clickable> clickables_;
    View view_ = View::a;
    std::size_t preset_ = 0;
    std::string notice_;
    struct Drag {
        bool active = false;
        int y = 0;
        double scale = 1;
    } drag_;
};

std::shared_ptr<TabScreen> make_processing_screen(UiContext& ui, std::shared_ptr<ProcessingModel> model);

}  // namespace signals::tui
