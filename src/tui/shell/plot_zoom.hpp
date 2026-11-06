#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"
#include "tui/widgets/axis_zoom.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {

class PlotZoom {
public:
    void begin_frame();
    AxisZoom& axis(const std::string& id, Axis full);
    ftxui::Element track(const std::string& id, PlotSpec spec, ftxui::Element plot);
    bool on_mouse(const ftxui::Mouse& m);
    bool on_key(const ftxui::Event& e, std::string_view id);
    bool press(int x, int y);
    std::optional<MenuItem> reset_item(int x, int y);
    std::optional<Axis> view(std::string_view id) const;
    ftxui::Box box(std::string_view id) const;

private:
    struct Entry {
        AxisZoom zoom;
        ftxui::Box box{};
        PlotSpec spec;
    };
    Entry* under(int x, int y);

    std::map<std::string, Entry, std::less<>> plots_;
    Entry* drag_ = nullptr;
    int drag_x_ = 0;
};

}  // namespace signals::tui
