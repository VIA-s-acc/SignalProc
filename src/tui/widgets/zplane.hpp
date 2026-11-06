#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

struct ZPlaneSpec {
    std::vector<std::complex<double>> zeros = {};
    std::ptrdiff_t poles_at_origin = 0;
    std::vector<std::complex<double>> poles = {};
    double extent = 1.5;
};

ftxui::Element zplane(ZPlaneSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs);

}  // namespace signals::tui
