#include "tui/screens/pipeline_canvas.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "i18n/i18n.hpp"
#include "pipeline/layout.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

enum : unsigned { up = 1, down = 2, left = 4, right = 8 };

int rank(CellStyle s) {
    switch (s) {
    case CellStyle::selected: return 3;
    case CellStyle::accent: return 2;
    case CellStyle::dim: return 0;
    default: return 1;
    }
}

std::string line_glyph(unsigned d, bool ascii) {
    const bool vertical = (d & (up | down)) != 0, horizontal = (d & (left | right)) != 0;
    if (ascii) return vertical && horizontal ? "+" : vertical ? "|" : "-";
    if (!horizontal) return "│";
    if (!vertical) return "─";
    static const std::map<unsigned, const char*> table{{up | right, "└"},        {up | left, "┘"},        {down | right, "┌"},
                                                       {down | left, "┐"},      {up | down | right, "├"}, {up | down | left, "┤"},
                                                       {up | left | right, "┴"}, {down | left | right, "┬"}};
    const auto it = table.find(d);
    return it == table.end() ? "┼" : it->second;
}

class Cells {
public:
    Cells(int w, int h) : w_{w}, h_{h}, dirs_(static_cast<std::size_t>(w * h), 0), text_(static_cast<std::size_t>(w * h)), style_(static_cast<std::size_t>(w * h), CellStyle::dim) {}

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    void link(int x, int y, unsigned d, CellStyle s) {
        if (!inside(x, y)) return;
        const auto i = at(x, y);
        dirs_[i] |= d;
        if (rank(s) > rank(style_[i])) style_[i] = s;
    }
    void put(int x, int y, const std::string& text, CellStyle s) {
        for (const auto& cp : split_codepoints(text)) {
            if (inside(x, y)) {
                text_[at(x, y)] = cp;
                style_[at(x, y)] = s;
            }
            ++x;
        }
    }
    void fill(GraphPicture& p, bool ascii) const {
        p.width = w_;
        p.height = h_;
        p.glyphs.resize(dirs_.size());
        p.styles = style_;
        for (std::size_t i = 0; i < dirs_.size(); ++i) p.glyphs[i] = !text_[i].empty() ? text_[i] : dirs_[i] ? line_glyph(dirs_[i], ascii) : " ";
    }

private:
    std::size_t at(int x, int y) const { return static_cast<std::size_t>(y * w_ + x); }
    int w_, h_;
    std::vector<unsigned> dirs_;
    std::vector<std::string> text_;
    std::vector<CellStyle> style_;
};

int port_row(int y, std::size_t k, std::size_t n, int h) {
    const int inner = h - 2;
    if (n <= 1) return y + 1 + (inner - 1) / 2;
    if (n == 2) return k == 0 ? y + 1 : y + h - 2;
    return y + 1 + static_cast<int>(std::min<std::size_t>(k, static_cast<std::size_t>(inner - 1)));
}

std::vector<std::pair<int, int>> polyline(const std::vector<std::pair<int, int>>& corners) {
    std::vector<std::pair<int, int>> cells;
    for (std::size_t i = 0; i + 1 < corners.size(); ++i) {
        auto [x, y] = corners[i];
        const auto [tx, ty] = corners[i + 1];
        while (x != tx || y != ty) {
            if (cells.empty() || cells.back() != std::pair{x, y}) cells.emplace_back(x, y);
            x += (tx > x) - (tx < x);
            if (x == tx) y += (ty > y) - (ty < y);
        }
    }
    if (!corners.empty()) cells.push_back(corners.back());
    return cells;
}

void stroke(Cells& c, const std::vector<std::pair<int, int>>& cells, CellStyle s) {
    for (std::size_t i = 0; i < cells.size(); ++i) {
        unsigned d = 0;
        for (const auto* other : {i > 0 ? &cells[i - 1] : nullptr, i + 1 < cells.size() ? &cells[i + 1] : nullptr}) {
            if (!other) continue;
            if (other->first < cells[i].first) d |= left;
            if (other->first > cells[i].first) d |= right;
            if (other->second < cells[i].second) d |= up;
            if (other->second > cells[i].second) d |= down;
        }
        if (cells.size() == 1) d = left | right;
        c.link(cells[i].first, cells[i].second, d, s);
    }
}

std::string state_glyph(pipeline::NodeState s, bool ascii) {
    switch (s) {
    case pipeline::NodeState::running: return std::string{glyph(Glyph::bullet_on, ascii ? GlyphMode::ascii : GlyphMode::unicode)};
    case pipeline::NodeState::done: return std::string{glyph(Glyph::check, ascii ? GlyphMode::ascii : GlyphMode::unicode)};
    case pipeline::NodeState::failed: return std::string{glyph(Glyph::cross, ascii ? GlyphMode::ascii : GlyphMode::unicode)};
    case pipeline::NodeState::bypassed: return ascii ? "-" : "–";
    case pipeline::NodeState::cached: return "=";
    default: return std::string{glyph(Glyph::bullet_off, ascii ? GlyphMode::ascii : GlyphMode::unicode)};
    }
}

CellStyle state_style(pipeline::NodeState s) {
    switch (s) {
    case pipeline::NodeState::running: return CellStyle::accent;
    case pipeline::NodeState::done:
    case pipeline::NodeState::cached: return CellStyle::ok;
    case pipeline::NodeState::failed: return CellStyle::err;
    default: return CellStyle::dim;
    }
}

std::string summary(const pipeline::GraphNode& n) {
    std::string out;
    int shown = 0;
    for (const auto& spec : n.type->params) {
        if (shown == 2) break;
        const auto it = n.params.find(spec.name);
        if (it == n.params.end()) continue;
        out += (out.empty() ? "" : " ") + spec.name + "=" + it->second;
        ++shown;
    }
    return out.empty() ? std::string{"—"} : out;
}

}  // namespace

std::string block_name(std::string_view type) {
    const auto key = "pipeline.block." + std::string{type};
    auto name = tr(key);
    return name == key ? std::string{type} : name;
}

GraphMetrics graph_metrics(int zoom) {
    switch (std::clamp(zoom, 0, kGraphZoomMax)) {
    case 0: return {14, 4, 4, 0, false, true};
    case 1: return {18, 4, 5, 1, true, false};
    case 3: return {34, 5, 8, 1, true, true};
    default: return {22, 5, 6, 1, true, true};
    }
}

GraphPicture draw_pipeline(const pipeline::Graph& g, const pipeline::PipelineDoc& doc, const RunView& run, const Selection& selection, GlyphMode mode, int zoom) {
    GraphPicture picture;
    if (g.nodes.empty()) return picture;
    const auto metrics = graph_metrics(zoom);
    const int kNodeWidth = metrics.node_w, kColumnGap = metrics.col_gap, kRowGap = metrics.row_gap;
    std::size_t most_ports = 1;
    for (const auto& n : g.nodes) most_ports = std::max({most_ports, n.type->inputs.size(), n.type->outputs.size()});
    const int kNodeHeight = std::max(metrics.node_h, static_cast<int>(std::min<std::size_t>(most_ports, 3)) + 2);
    const auto port_at = [&](int y, std::size_t k, std::size_t n) { return port_row(y, k, n, kNodeHeight); };
    const bool ascii = mode == GlyphMode::ascii;
    const auto layout = pipeline::layout_graph(g);
    std::size_t rows = 1;
    for (const auto& layer : layout.layers) rows = std::max(rows, layer.size());
    const int width = 2 + static_cast<int>(layout.layers.size()) * (kNodeWidth + kColumnGap) - kColumnGap;
    const int height = static_cast<int>(rows) * (kNodeHeight + kRowGap);
    Cells cells{width, height};
    const auto col = [&](int layer) { return 1 + layer * (kNodeWidth + kColumnGap); };
    const auto row = [&](int order) { return order * (kNodeHeight + kRowGap); };
    const auto item_x = [&](std::size_t item) { return col(layout.items[item].layer); };
    const auto item_y = [&](std::size_t item) { return row(layout.items[item].order); };

    std::map<std::size_t, int> lane;
    for (const auto& layer : layout.layers)
        for (std::size_t k = 0; k < layer.size(); ++k)
            if (!layout.items[layer[k]].next.empty()) lane[layer[k]] = static_cast<int>(lane.size() % (kColumnGap - 2));

    for (std::size_t v = 0; v < g.nodes.size(); ++v) {
        const auto& target = g.nodes[v];
        std::set<std::size_t> from;
        for (const auto& s : target.sources)
            if (s) from.insert(s->node);
        for (const auto u : from) {
            std::vector<std::size_t> chain;
            for (const auto first : layout.items[layout.item_of_node[u]].next) {
                std::vector<std::size_t> walk{layout.item_of_node[u], first};
                while (layout.items[walk.back()].node < 0) walk.push_back(layout.items[walk.back()].next.front());
                if (static_cast<std::size_t>(layout.items[walk.back()].node) == v) {
                    chain = std::move(walk);
                    break;
                }
            }
            if (chain.size() < 2) continue;
            for (std::size_t j = 0; j < target.sources.size(); ++j) {
                if (!target.sources[j] || target.sources[j]->node != u) continue;
                const auto& source = *target.sources[j];
                const auto& from_ref = *target.inputs[j];
                pipeline::Edge edge{from_ref, {target.id, target.type->inputs[j].name, 0, 0}};
                for (const auto& e : doc.edges)
                    if (same_port(e.from, from_ref) && e.to.node == target.id && (e.to.port == target.type->inputs[j].name || (e.to.port.empty() && target.type->inputs.size() == 1)))
                        edge = e;
                const bool chosen = selection.kind == Selection::Kind::wire && same_wire(selection.wire, edge);
                const auto style = chosen ? CellStyle::selected : CellStyle::dim;
                WireHit hit{edge, {}};
                for (std::size_t h = 0; h + 1 < chain.size(); ++h) {
                    const auto a = chain[h], b = chain[h + 1];
                    const int sx = item_x(a) + kNodeWidth;
                    const int sy = h == 0 ? port_at(item_y(a), source.output, g.nodes[u].type->outputs.size()) : port_at(item_y(a), 0, 1);
                    const bool last = h + 2 == chain.size();
                    const int ex = item_x(b) - 1;
                    const int ey = last ? port_at(item_y(b), j, target.type->inputs.size()) : port_at(item_y(b), 0, 1);
                    const int lx = std::min(ex - 1, sx + 1 + lane[a]);
                    auto path = sy == ey ? polyline({{sx, sy}, {ex, ey}}) : polyline({{sx, sy}, {lx, sy}, {lx, ey}, {ex, ey}});
                    if (!last) {
                        const auto through = polyline({{item_x(b), ey}, {item_x(b) + kNodeWidth - 1, ey}});
                        path.insert(path.end(), through.begin(), through.end());
                    }
                    stroke(cells, path, style);
                    hit.cells.insert(hit.cells.end(), path.begin(), path.end());
                    if (last) cells.put(ex, ey, ascii ? ">" : "▶", style);
                    if (h == 0 && !source.band.empty()) cells.put(sx, sy - 1, source.band, CellStyle::dim);
                }
                picture.wires.push_back(std::move(hit));
            }
        }
    }

    for (const auto& probe : g.probes) {
        const auto& n = g.nodes[probe.source.node];
        const auto item = layout.item_of_node[probe.source.node];
        const int y = port_at(item_y(item), probe.source.output, n.type->outputs.size());
        cells.put(item_x(item) + kNodeWidth, y, ascii ? "#" : "◆", CellStyle::accent);
    }

    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const auto& n = g.nodes[i];
        const auto item = layout.item_of_node[i];
        const int x = item_x(item), y = item_y(item);
        const auto it = run.nodes.find(n.id);
        const auto state = it == run.nodes.end() ? pipeline::NodeState::waiting : it->second.status.state;
        const bool chosen = selection.kind == Selection::Kind::node && selection.node == n.id;
        const bool slow = it != run.nodes.end() && it->second.bottleneck;
        const auto frame = chosen ? CellStyle::selected : state == pipeline::NodeState::failed ? CellStyle::err : slow ? CellStyle::warn : CellStyle::plain;
        const std::string h = n.bypass ? (ascii ? "." : "┄") : ascii ? "-" : "─", vert = n.bypass ? (ascii ? ":" : "┆") : ascii ? "|" : "│";
        std::string top = ascii ? "+" : "┌", bottom = ascii ? "+" : "└";
        for (int k = 0; k < kNodeWidth - 2; ++k) top += h, bottom += h;
        top += ascii ? "+" : "┐";
        bottom += ascii ? "+" : "┘";
        cells.put(x, y, top, frame);
        cells.put(x, y + kNodeHeight - 1, bottom, frame);
        for (int k = 1; k < kNodeHeight - 1; ++k) {
            cells.put(x, y + k, vert, frame);
            cells.put(x + kNodeWidth - 1, y + k, vert, frame);
            cells.put(x + 1, y + k, std::string(static_cast<std::size_t>(kNodeWidth - 2), ' '), CellStyle::plain);
        }
        const int inner = kNodeWidth - 4;
        cells.put(x + 2, y + 1, state_glyph(state, ascii), state_style(state));
        cells.put(x + 4, y + 1, ellipsize(n.id + (ascii ? " " : " · ") + block_name(n.type->id), inner - 2, ascii), chosen ? CellStyle::selected : CellStyle::plain);
        if (metrics.summary) cells.put(x + 2, y + 2, ellipsize(summary(n), inner, ascii), CellStyle::dim);
        std::string info;
        auto info_style = CellStyle::dim;
        if (it != run.nodes.end()) {
            const auto& s = it->second.status;
            switch (s.state) {
            case pipeline::NodeState::running:
                info = eighths_bar(s.fraction, 10) + " " + format_fixed(100 * s.fraction, 0) + " %";
                info_style = CellStyle::accent;
                break;
            case pipeline::NodeState::done: {
                const auto shape = shape_text(s.shapes, true);
                info = (shape.empty() ? std::string{} : shape + " · ") + format_si(s.wall_seconds, tr("unit.s")) + (shape.empty() ? " · " + format_si(static_cast<double>(s.bytes), tr("unit.byte")) : std::string{});
                break;
            }
            case pipeline::NodeState::cached: {
                const auto shape = shape_text(s.shapes, true);
                info = tr("tui.pipeline.cached") + " · " + (shape.empty() ? format_si(static_cast<double>(s.bytes), tr("unit.byte")) : shape);
                break;
            }
            case pipeline::NodeState::failed:
                info = s.message;
                info_style = CellStyle::err;
                break;
            case pipeline::NodeState::bypassed: info = tr("tui.pipeline.state.bypassed"); break;
            case pipeline::NodeState::waiting: info = tr("tui.pipeline.state.waiting"); break;
            }
        }
        if (n.bypass && info.empty()) info = tr("tui.pipeline.state.bypassed");
        if (metrics.info) cells.put(x + 2, y + (metrics.summary ? 3 : 2), ellipsize(info, inner, ascii), info_style);
        for (std::size_t k = 0; k < n.type->inputs.size(); ++k) {
            const int py = port_at(y, k, n.type->inputs.size());
            cells.put(x, py, std::string{glyph(Glyph::bullet_off, mode)}, CellStyle::plain);
            picture.ports.push_back({n.id, n.type->inputs[k].name, false, x, py});
        }
        for (std::size_t k = 0; k < n.type->outputs.size(); ++k) {
            const int py = port_at(y, k, n.type->outputs.size());
            cells.put(x + kNodeWidth - 1, py, std::string{glyph(Glyph::bullet_on, mode)}, CellStyle::plain);
            picture.ports.push_back({n.id, n.type->outputs[k].name, true, x + kNodeWidth - 1, py});
        }
        picture.nodes.push_back({n.id, x, y, kNodeWidth, kNodeHeight});
    }
    cells.fill(picture, ascii);
    return picture;
}

}  // namespace signals::tui
