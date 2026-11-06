#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "theory/latex.hpp"

namespace signals::theory {

enum class MathStyle { display, inline_linear };
enum class MathGlyphs { unicode, ascii };

struct MathBox {
    std::vector<std::string> lines;
    int baseline = 0;
    int width = 0;
};

MathBox layout(const MathNode& node, MathStyle style, MathGlyphs glyphs);
std::expected<MathBox, LatexError> render_latex(std::string_view src, MathStyle style, MathGlyphs glyphs);

}  // namespace signals::theory
