#include "pipeline/layout.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace signals::pipeline {
namespace {

enum : unsigned { up = 1, down = 2, left = 4, right = 8 };

std::vector<std::string> codepoints(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

class Canvas {
public:
    Canvas(int width, int height) : width_{width}, cells_(static_cast<std::size_t>(width * height)) {}

    void link(int x, int y, unsigned dirs) {
        if (auto* c = at(x, y)) c->dirs |= dirs;
    }
    void put(int x, int y, const std::string& text) {
        for (const auto& cp : codepoints(text)) {
            if (auto* c = at(x++, y)) c->text = cp;
        }
    }
    std::vector<std::string> lines(bool ascii) const {
        std::vector<std::string> out;
        const int height = static_cast<int>(cells_.size()) / width_;
        for (int y = 0; y < height; ++y) {
            std::string line;
            for (int x = 0; x < width_; ++x) line += glyph(cells_[static_cast<std::size_t>(y * width_ + x)], ascii);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            out.push_back(std::move(line));
        }
        return out;
    }

private:
    struct Cell {
        unsigned dirs = 0;
        std::string text;
    };

    Cell* at(int x, int y) {
        if (x < 0 || x >= width_ || y < 0 || y * width_ >= static_cast<int>(cells_.size())) return nullptr;
        return &cells_[static_cast<std::size_t>(y * width_ + x)];
    }

    static std::string glyph(const Cell& c, bool ascii) {
        if (!c.text.empty()) return c.text;
        const auto d = c.dirs;
        if (d == 0) return " ";
        const bool vertical = (d & (up | down)) != 0, horizontal = (d & (left | right)) != 0;
        if (ascii) return vertical && horizontal ? "+" : vertical ? "|" : "-";
        if (!horizontal) return "│";
        if (!vertical) return "─";
        static const std::map<unsigned, const char*> table{
            {up | right, "└"},          {up | left, "┘"},          {down | right, "┌"},      {down | left, "┐"},
            {up | down | right, "├"},   {up | down | left, "┤"},   {up | left | right, "┴"}, {down | left | right, "┬"},
            {up | down | left | right, "┼"}};
        const auto it = table.find(d);
        return it == table.end() ? "┼" : it->second;
    }

    int width_;
    std::vector<Cell> cells_;
};

struct Item {
    int node = -1;
    std::string label;
    int width = 1;
    int center = 0;
    std::vector<std::size_t> below;
};

int text_width(const std::string& s) { return static_cast<int>(codepoints(s).size()); }

std::string port_text(const PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; }

}  // namespace

GraphLayout layout_graph(const Graph& g) {
    GraphLayout out;
    const auto n = g.nodes.size();
    if (n == 0) return out;
    std::vector<int> layer(n, 0);
    for (const auto i : g.order)
        for (const auto& s : g.nodes[i].sources)
            if (s) layer[i] = std::max(layer[i], layer[s->node] + 1);
    const int depth = *std::max_element(layer.begin(), layer.end()) + 1;

    auto& items = out.items;
    std::vector<std::vector<std::size_t>> above;
    out.layers.assign(static_cast<std::size_t>(depth), {});
    out.item_of_node.resize(n);
    const auto add = [&](int node, int l) {
        items.push_back({node, l, 0, {}});
        above.emplace_back();
        out.layers[static_cast<std::size_t>(l)].push_back(items.size() - 1);
        return items.size() - 1;
    };
    for (std::size_t i = 0; i < n; ++i) out.item_of_node[i] = add(static_cast<int>(i), layer[i]);
    const auto connect = [&](std::size_t a, std::size_t b) {
        items[a].next.push_back(b);
        above[b].push_back(a);
    };
    for (std::size_t v = 0; v < n; ++v) {
        std::set<std::size_t> from;
        for (const auto& s : g.nodes[v].sources)
            if (s) from.insert(s->node);
        for (const auto u : from) {
            auto prev = out.item_of_node[u];
            for (int l = layer[u] + 1; l < layer[v]; ++l) {
                const auto dummy = add(-1, l);
                connect(prev, dummy);
                prev = dummy;
            }
            connect(prev, out.item_of_node[v]);
        }
    }

    auto& rows = out.layers;
    for (int pass = 0; pass < 4; ++pass) {
        const bool downward = pass % 2 == 0;
        for (int step = 1; step < depth; ++step) {
            const auto l = static_cast<std::size_t>(downward ? step : depth - 1 - step);
            const auto& ref = rows[downward ? l - 1 : l + 1];
            std::map<std::size_t, double> position;
            for (std::size_t k = 0; k < ref.size(); ++k) position[ref[k]] = static_cast<double>(k);
            std::map<std::size_t, double> key;
            for (std::size_t k = 0; k < rows[l].size(); ++k) {
                const auto id = rows[l][k];
                const auto& links = downward ? above[id] : items[id].next;
                double sum = 0;
                for (const auto other : links) sum += position[other];
                key[id] = links.empty() ? static_cast<double>(k) : sum / static_cast<double>(links.size());
            }
            std::stable_sort(rows[l].begin(), rows[l].end(), [&](std::size_t a, std::size_t b) { return key[a] < key[b]; });
        }
    }
    for (const auto& row : rows)
        for (std::size_t k = 0; k < row.size(); ++k) items[row[k]].order = static_cast<int>(k);
    return out;
}

int crossings(const GraphLayout& l) {
    int total = 0;
    for (const auto& row : l.layers) {
        std::vector<std::pair<int, int>> edges;
        for (const auto id : row)
            for (const auto next : l.items[id].next) edges.emplace_back(l.items[id].order, l.items[next].order);
        for (std::size_t a = 0; a < edges.size(); ++a)
            for (std::size_t b = a + 1; b < edges.size(); ++b)
                if ((edges[a].first < edges[b].first && edges[a].second > edges[b].second) ||
                    (edges[a].first > edges[b].first && edges[a].second < edges[b].second))
                    ++total;
    }
    return total;
}

std::vector<std::string> draw_graph(const Graph& g, bool ascii, int max_width) {
    const auto n = g.nodes.size();
    if (n == 0) return {};
    const auto layout = layout_graph(g);
    const int depth = static_cast<int>(layout.layers.size());
    std::vector<Item> items;
    for (const auto& it : layout.items) items.push_back({it.node, {}, 1, 0, it.next});
    const auto& rows = layout.layers;

    constexpr int kGap = 3;
    int width = 0;
    for (const bool full : {true, false}) {
        for (auto& it : items) {
            if (it.node < 0) continue;
            const auto& node = g.nodes[static_cast<std::size_t>(it.node)];
            it.label = full ? "[" + node.id + (ascii ? ": " : " · ") + node.type->id + "]" : "[" + node.id + "]";
            it.width = text_width(it.label);
        }
        width = 0;
        for (const auto& row : rows) {
            int total = 0;
            for (const auto id : row) total += items[id].width;
            total += kGap * static_cast<int>(row.size() - 1);
            width = std::max(width, total);
        }
        if (width <= max_width) break;
    }
    for (const auto& row : rows) {
        int total = 0;
        for (const auto id : row) total += items[id].width;
        total += kGap * static_cast<int>(row.size() - 1);
        int x = (width - total) / 2;
        for (const auto id : row) {
            items[id].center = x + items[id].width / 2;
            x += items[id].width + kGap;
        }
    }

    std::vector<int> tracks(static_cast<std::size_t>(depth), 0), top(static_cast<std::size_t>(depth), 0);
    std::map<std::size_t, int> track_of;
    for (int l = 0; l < depth; ++l)
        for (const auto id : rows[static_cast<std::size_t>(l)]) {
            const auto& it = items[id];
            if (std::any_of(it.below.begin(), it.below.end(), [&](std::size_t c) { return items[c].center != it.center; }))
                track_of[id] = tracks[static_cast<std::size_t>(l)]++;
        }
    for (int l = 1; l < depth; ++l) top[static_cast<std::size_t>(l)] = top[static_cast<std::size_t>(l - 1)] + 3 + std::max(1, tracks[static_cast<std::size_t>(l - 1)]);
    const int height = top.back() + 1;

    Canvas canvas{std::max(width, 1), height};
    for (int l = 0; l < depth; ++l) {
        const int y = top[static_cast<std::size_t>(l)];
        const int lanes = std::max(1, tracks[static_cast<std::size_t>(l)]);
        const int arrows = y + 2 + lanes;
        for (const auto id : rows[static_cast<std::size_t>(l)]) {
            const auto& it = items[id];
            if (it.node >= 0) canvas.put(it.center - it.width / 2, y, it.label);
            else canvas.link(it.center, y, up | down);
            if (it.below.empty()) continue;
            const int px = it.center;
            const auto lane = track_of.find(id);
            const int turn = lane == track_of.end() ? arrows : y + 2 + lane->second;
            for (int row = y + 1; row < turn; ++row) canvas.link(px, row, up | down);
            for (const auto child : it.below) {
                const int cx = items[child].center;
                if (cx == px) {
                    for (int row = turn; row < arrows; ++row) canvas.link(px, row, up | down);
                } else {
                    const unsigned toward = cx > px ? right : left, back = cx > px ? left : right;
                    canvas.link(px, turn, up | toward);
                    for (int x = std::min(px, cx) + 1; x < std::max(px, cx); ++x) canvas.link(x, turn, left | right);
                    canvas.link(cx, turn, down | back);
                    for (int row = turn + 1; row < arrows; ++row) canvas.link(cx, row, up | down);
                }
                if (items[child].node >= 0) canvas.put(cx, arrows, ascii ? "v" : "▼");
                else canvas.link(cx, arrows, up | down);
            }
        }
    }
    auto out = canvas.lines(ascii);
    const std::string arrow = ascii ? " -> " : " → ";
    std::vector<std::string> notes;
    for (std::size_t v = 0; v < n; ++v)
        for (std::size_t k = 0; k < g.nodes[v].inputs.size(); ++k) {
            const auto& from = g.nodes[v].inputs[k];
            if (!from) continue;
            const auto& port = g.nodes[v].type->inputs[k].name;
            const bool named = !from->port.empty() || g.nodes[v].type->inputs.size() > 1;
            if (named) notes.push_back("  " + port_text(*from) + arrow + g.nodes[v].id + "." + port);
        }
    for (const auto& p : g.probes) notes.push_back("  probe " + port_text(p.at) + (ascii ? " as " : " · ") + p.name);
    if (!notes.empty()) {
        out.emplace_back();
        for (auto& note : notes) out.push_back(std::move(note));
    }
    for (auto& line : out) {
        auto cps = codepoints(line);
        if (static_cast<int>(cps.size()) <= max_width) continue;
        cps.resize(static_cast<std::size_t>(max_width));
        line.clear();
        for (const auto& cp : cps) line += cp;
    }
    return out;
}

}  // namespace signals::pipeline
