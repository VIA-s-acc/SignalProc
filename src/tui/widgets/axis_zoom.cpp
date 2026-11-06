#include "tui/widgets/axis_zoom.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace signals::tui {
namespace {

constexpr double kMinSpan = 1e-3;

}  // namespace

void AxisZoom::zoom(double factor, double anchor) {
    if (!(factor > 0)) return;
    const double span = hi_ - lo_;
    const double s = std::clamp(span / factor, kMinSpan, 1.0);
    const double a = std::clamp(anchor, lo_, hi_);
    lo_ = std::clamp(a - (a - lo_) * s / span, 0.0, 1.0 - s);
    hi_ = lo_ + s;
}

void AxisZoom::zoom_at(double factor, const ftxui::Box& area, int x) { zoom(factor, fraction_at(area, x)); }

void AxisZoom::pan(double fraction) {
    const double span = hi_ - lo_;
    lo_ = std::clamp(lo_ + fraction, 0.0, 1.0 - span);
    hi_ = lo_ + span;
}

void AxisZoom::reset() { lo_ = 0, hi_ = 1; }

bool AxisZoom::zoomed() const { return lo_ > 1e-12 || hi_ < 1 - 1e-12; }

bool AxisZoom::in_pi_fractions() const { return full_.scale == AxisScale::pi && hi_ - lo_ < 0.5 - 1e-12; }

Axis AxisZoom::axis() const {
    if (full_.scale == AxisScale::log10 && full_.lo > 0 && full_.hi > full_.lo) {
        const double l = std::log10(full_.lo), h = std::log10(full_.hi);
        return {std::pow(10.0, l + lo_ * (h - l)), std::pow(10.0, l + hi_ * (h - l)), AxisScale::log10};
    }
    const double span = full_.hi - full_.lo;
    const double a = full_.lo + lo_ * span, b = full_.lo + hi_ * span;
    if (in_pi_fractions()) return {a / std::numbers::pi, b / std::numbers::pi, AxisScale::linear};
    return {a, b, full_.scale};
}

double AxisZoom::plot_x(double v) const { return in_pi_fractions() ? v / std::numbers::pi : v; }

double AxisZoom::fraction_at(const ftxui::Box& area, int x) const {
    const int columns = std::max(1, area.x_max - area.x_min + 1);
    double v = value_at_column(axis(), x - area.x_min, columns);
    if (in_pi_fractions()) v *= std::numbers::pi;
    if (full_.scale == AxisScale::log10 && full_.lo > 0 && full_.hi > full_.lo && v > 0) {
        const double l = std::log10(full_.lo), h = std::log10(full_.hi);
        return std::clamp((std::log10(v) - l) / (h - l), 0.0, 1.0);
    }
    const double span = full_.hi - full_.lo;
    return span > 0 ? std::clamp((v - full_.lo) / span, 0.0, 1.0) : 0.0;
}

}  // namespace signals::tui
