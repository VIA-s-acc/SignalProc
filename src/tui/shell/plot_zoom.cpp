#include "tui/shell/plot_zoom.hpp"

#include <utility>

#include "i18n/i18n.hpp"

namespace signals::tui {
namespace {

constexpr double kWheelStep = 1.25;
const ftxui::Box kNone{0, -1, 0, -1};

}  // namespace

void PlotZoom::begin_frame() {
    for (auto& [id, e] : plots_) e.box = kNone;
}

AxisZoom& PlotZoom::axis(const std::string& id, Axis full) {
    auto it = plots_.find(id);
    if (it == plots_.end()) it = plots_.emplace(id, Entry{AxisZoom{full}, kNone, {}}).first;
    it->second.zoom.set_full(full);
    return it->second.zoom;
}

ftxui::Element PlotZoom::track(const std::string& id, PlotSpec spec, ftxui::Element plot) {
    auto& e = plots_[id];
    spec.traces.clear();
    spec.envelopes.clear();
    e.spec = std::move(spec);
    return plot | ftxui::reflect(e.box);
}

PlotZoom::Entry* PlotZoom::under(int x, int y) {
    for (auto& [id, e] : plots_)
        if (e.box.Contain(x, y)) return &e;
    return nullptr;
}

bool PlotZoom::on_mouse(const ftxui::Mouse& m) {
    using ftxui::Mouse;
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        auto* e = under(m.x, m.y);
        if (!e) return false;
        e->zoom.zoom_at(m.button == Mouse::WheelUp ? kWheelStep : 1 / kWheelStep, plot_area(e->box, e->spec), m.x);
        return true;
    }
    if (!drag_ || m.motion == Mouse::Pressed) {
        drag_ = nullptr;
        return false;
    }
    if (m.motion == Mouse::Released || m.button != Mouse::Left) {
        drag_ = nullptr;
        return m.motion == Mouse::Released;
    }
    if (m.x != drag_x_) {
        const auto area = plot_area(drag_->box, drag_->spec);
        drag_->zoom.pan(drag_->zoom.fraction_at(area, drag_x_) - drag_->zoom.fraction_at(area, m.x));
        drag_x_ = m.x;
    }
    return true;
}

bool PlotZoom::press(int x, int y) {
    drag_ = under(x, y);
    drag_x_ = x;
    return drag_ != nullptr;
}

std::optional<MenuItem> PlotZoom::reset_item(int x, int y) {
    auto* e = under(x, y);
    if (!e || !e->zoom.zoomed()) return std::nullopt;
    return MenuItem{tr("tui.menu.reset_zoom"), "", [e] { e->zoom.reset(); }};
}

std::optional<Axis> PlotZoom::view(std::string_view id) const {
    const auto it = plots_.find(id);
    if (it == plots_.end()) return std::nullopt;
    return it->second.zoom.axis();
}

ftxui::Box PlotZoom::box(std::string_view id) const {
    const auto it = plots_.find(id);
    return it == plots_.end() ? kNone : it->second.box;
}

}  // namespace signals::tui
