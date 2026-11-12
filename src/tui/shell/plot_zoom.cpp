#include "tui/shell/plot_zoom.hpp"

#include <utility>

#include "i18n/i18n.hpp"
#include "tui/shell/plot_export.hpp"

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

ftxui::Element PlotZoom::track(const std::string& id, PlotSpec spec, ftxui::Element plot, PlotMeta meta) {
    auto& e = plots_[id];
    e.spec = std::move(spec);
    e.meta = std::move(meta);
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

bool PlotZoom::on_key(const ftxui::Event& e, std::string_view id) {
    using ftxui::Event;
    const auto it = plots_.find(id);
    if (it == plots_.end()) return false;
    auto& z = it->second.zoom;
    if (e == Event::Character('+') || e == Event::Character('=')) return z.zoom_center(kWheelStep), true;
    if (e == Event::Character('-')) return z.zoom_center(1 / kWheelStep), true;
    if (e == Event::Character('>') || e == Event::Character('.')) return z.pan_view(0.2), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return z.pan_view(-0.2), true;
    if (e == Event::Character('0')) return z.reset(), true;
    return false;
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

std::vector<MenuItem> PlotZoom::menu(int x, int y, UiContext& ui) {
    std::vector<MenuItem> out;
    auto* e = under(x, y);
    if (!e) return out;
    if (auto reset = reset_item(x, y)) out.push_back(*reset);
    for (auto& [id, entry] : plots_)
        if (&entry == e) {
            out.push_back(export_item(ui, {id, entry.meta.title.empty() ? id : entry.meta.title, entry.spec, entry.meta.names, entry.meta.x_label, entry.meta.y_label}));
        }
    return out;
}

std::vector<PlotExport> PlotZoom::exports() const {
    std::vector<PlotExport> out;
    for (const auto& [id, e] : plots_)
        if (e.box.x_max >= e.box.x_min) out.push_back({id, e.meta.title.empty() ? id : e.meta.title, e.spec, e.meta.names, e.meta.x_label, e.meta.y_label});
    return out;
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
