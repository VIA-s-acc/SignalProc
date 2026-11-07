#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {

enum class CellStyle : std::uint8_t { plain, dim, accent, ok, warn, err, selected };

struct NodeHit {
    std::string id;
    int x = 0, y = 0, w = 0, h = 0;
};

struct PortHit {
    std::string node, port;
    bool output = false;
    int x = 0, y = 0;
};

struct WireHit {
    pipeline::Edge edge;
    std::vector<std::pair<int, int>> cells;
};

struct GraphPicture {
    int width = 0, height = 0;
    std::vector<std::string> glyphs;
    std::vector<CellStyle> styles;
    std::vector<NodeHit> nodes;
    std::vector<PortHit> ports;
    std::vector<WireHit> wires;

    const std::string& glyph(int x, int y) const { return glyphs[static_cast<std::size_t>(y * width + x)]; }
    CellStyle style(int x, int y) const { return styles[static_cast<std::size_t>(y * width + x)]; }
};

inline constexpr int kNodeWidth = 22, kNodeHeight = 5, kColumnGap = 6, kRowGap = 1;

GraphPicture draw_pipeline(const pipeline::Graph& g, const pipeline::PipelineDoc& doc, const RunView& run, const Selection& selection,
                           GlyphMode mode);
std::string block_name(std::string_view type);

}  // namespace signals::tui
