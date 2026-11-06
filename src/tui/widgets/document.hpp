#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "theory/live.hpp"
#include "theory/markdown.hpp"
#include "theory/math_layout.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

inline constexpr ftxui::Box kNoDocBox{0, -1, 0, -1};

struct DocLink {
    std::string target;
    std::string label;
    ftxui::Box box = kNoDocBox;
    int line = -1;
};

struct DocFormula {
    std::string latex;
    ftxui::Box box = kNoDocBox;
};

struct DocumentSpec {
    std::vector<theory::Block> blocks;
    int width = 80;
    int scroll = 0, hscroll = 0;
    theory::MathGlyphs glyphs = theory::MathGlyphs::unicode;
    std::optional<std::size_t> focused_link = {};
    const theory::LiveValues* values = nullptr;
    std::string section_rule = {};
    int height = 0;
};

struct DocumentLayout {
    int height = 0;
    std::vector<DocLink> links;
    std::vector<DocFormula> formulas;
    std::vector<std::string> lines;
    int max_hscroll = 0;
};

ftxui::Element document(const DocumentSpec& spec, const UiContext& ui, DocumentLayout* out);
std::string cut_columns(const std::string& s, int from, int count);

}  // namespace signals::tui
