#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

struct ZMarker {
    std::complex<double> z;
    std::string glyph;
    Rgb color;
    int id = -1;
};

struct ZHit {
    int id;
    int x;
    int y;
};

struct ZPlaneSpec {
    std::vector<std::complex<double>> zeros = {};
    std::ptrdiff_t poles_at_origin = 0;
    std::vector<std::complex<double>> poles = {};
    double extent = 1.5;
    std::complex<double> center = {};
    std::vector<ZMarker> markers = {};
    std::shared_ptr<std::vector<ZHit>> hits = {};
    std::optional<std::string> minus_one_label = {};
};

ftxui::Element zplane(ZPlaneSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs);

class ZView {
public:
    double extent(double base) const { return base / zoom_; }
    std::complex<double> center() const { return center_; }
    bool zoomed() const { return zoom_ != 1.0 || center_ != std::complex<double>{}; }
    void reset() { zoom_ = 1.0, center_ = {}, dragging_ = false; }
    void apply(ZPlaneSpec& spec) const { spec.extent = extent(spec.extent), spec.center = center_; }
    std::complex<double> at(const ftxui::Box& box, int x, int y, double base) const;
    bool on_mouse(const ftxui::Mouse& m, const ftxui::Box& box, double base);
    bool on_key(const ftxui::Event& e, double base);

private:
    void zoom_by(double factor, std::optional<std::complex<double>> anchor, double base);

    double zoom_ = 1.0;
    std::complex<double> center_{};
    bool dragging_ = false;
    int last_x_ = 0, last_y_ = 0;
};

}  // namespace signals::tui
