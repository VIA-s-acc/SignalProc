#pragma once

#include <ftxui/screen/box.hpp>

#include "tui/widgets/plot.hpp"

namespace signals::tui {

class AxisZoom {
public:
    explicit AxisZoom(Axis full = {}) : full_{full} {}

    void set_full(Axis full) { full_ = full; }
    void zoom(double factor, double anchor);
    void zoom_at(double factor, const ftxui::Box& area, int x);
    void pan(double fraction);
    void zoom_center(double factor) { zoom(factor, (lo_ + hi_) / 2); }
    void pan_view(double share) { pan(share * (hi_ - lo_)); }
    void show(double fraction) {
        if (fraction < lo_) pan(fraction - lo_);
        if (fraction > hi_) pan(fraction - hi_);
    }
    void reset();
    bool zoomed() const;
    Axis axis() const;
    double plot_x(double v) const;
    double fraction_at(const ftxui::Box& area, int x) const;

private:
    bool in_pi_fractions() const;

    Axis full_;
    double lo_ = 0, hi_ = 1;
};

}  // namespace signals::tui
