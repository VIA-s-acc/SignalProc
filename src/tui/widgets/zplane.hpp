#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

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
    std::vector<ZMarker> markers = {};
    std::shared_ptr<std::vector<ZHit>> hits = {};
    std::optional<std::string> minus_one_label = {};
};

ftxui::Element zplane(ZPlaneSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs);

}  // namespace signals::tui
