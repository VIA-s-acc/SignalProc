#include "tui/widgets/bank_diagram.hpp"

#include <string_view>
#include <utility>
#include <vector>

#include "tui/shell/panel.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

enum class Kind { line, label, block };

struct Segment {
    std::string text;
    const char* id = nullptr;
    Kind kind = Kind::line;
};

using Row = std::vector<Segment>;

Segment line(std::string text, const char* id = nullptr) { return {std::move(text), id, Kind::line}; }
Segment label(std::string text, const char* id) { return {std::move(text), id, Kind::label}; }
Segment block(std::string text, const char* id) { return {std::move(text), id, Kind::block}; }

std::string repeat(std::string_view s, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

struct Widths {
    int lead, tail;
    int middle() const { return lead + tail + 4; }
};

Widths widths(const DiagramSpec& s) { return s.compact ? Widths{1, 2} : Widths{7, 13}; }

void middle_wire(Row& row, const Widths& w, const char* text, const char* id) {
    row.push_back(line(repeat("─", w.lead) + " "));
    row.push_back(label(text, id));
    row.push_back(line(" " + repeat("─", w.tail) + "▶"));
}

std::vector<Row> direct_layout(const DiagramSpec& s) {
    const auto w = widths(s);
    const std::string down = "│ " + s.down + " │", up = "│ " + s.up + " │";
    const std::string gap(static_cast<std::size_t>(w.middle()), ' ');
    auto lid = [&](const char* top, const char* id0, const char* id1, const char* id2, const char* id3, const char* tail) {
        Row r{line("              "), block(top, id0), line("      "), block(top, id1), line(gap), block(top, id2), line("      "), block(top, id3)};
        if (tail) r.push_back(line(tail));
        return r;
    };
    auto branch = [&](const char* head, const char* h, const char* hid, const char* y, const char* yid, const char* did, const char* band,
                      const char* bid, const char* uid, const char* u, const char* uwire, const char* f, const char* fid, const char* end,
                      const char* vid) {
        Row r{line(head), block(h, hid), line("─"), label(y, yid), line("▶"), block(down, did)};
        middle_wire(r, w, band, bid);
        r.insert(r.end(), {block(up, uid), line("─"), label(u, uwire), line("▶"), block(f, fid), line(end, vid)});
        return r;
    };
    return {
        lid("╭────╮", "H0", "D0", "U0", "F0", nullptr),
        branch("           ┌─▶", "│ H₀ │", "H0", " y₀ ", "y0", "D0", "a", "a", "U0", " u₀ ", "u0", "│ F₀ │", "F0", "──┐", "v0"),
        Row{line("  "), label("x", "x"), line(" ───────┤  "), block("╰────╯", "H0"), line("      "), block("╰────╯", "D0"), line(gap),
            block("╰────╯", "U0"), line("      "), block("╰────╯", "F0"), line("  │", "v0")},
        Row{line("           │" + std::string(static_cast<std::size_t>(40 + w.middle()), ' ')), block("⊕", "S"), line("──▶ "), label("x̂", "xh")},
        Row{line("           │  "), block("╭────╮", "H1"), line("      "), block("╭────╮", "D1"), line(gap), block("╭────╮", "U1"), line("      "),
            block("╭────╮", "F1"), line("  │", "v1")},
        branch("           └─▶", "│ H₁ │", "H1", " y₁ ", "y1", "D1", "d", "d", "U1", " u₁ ", "u1", "│ F₁ │", "F1", "──┘", "v1"),
        lid("╰────╯", "H1", "D1", "U1", "F1", nullptr),
    };
}

std::vector<Row> polyphase_layout(const DiagramSpec& s) {
    const auto w = widths(s);
    const int in = s.compact ? 7 : 12;
    const std::string lead(static_cast<std::size_t>(in), ' '), gap(static_cast<std::size_t>(w.middle()), ' '), side(8, ' ');
    auto lid = [&](const char* small, const char* wide) {
        return Row{line(lead), block(small, "SPLIT"), line(side), block(wide, "E"), line(gap), block(wide, "R"), line(side), block(small, "MERGE")};
    };
    auto branch = [&](Row head, const char* split, const char* xw, const char* xid, const char* band, const char* bid, const char* ww,
                      const char* wid, const char* merge, const char* end) {
        head.insert(head.end(), {block(split, "SPLIT"), line("─ "), label(xw, xid), line(" ──▶"), block("│        │", "E")});
        middle_wire(head, w, band, bid);
        head.insert(head.end(), {block("│        │", "R"), line("─ "), label(ww, wid), line(" ──▶"), block(merge, "MERGE"), line(end, "xh")});
        return head;
    };
    return {
        lid("╭──────╮", "╭────────╮"),
        branch(Row{line("  "), label("x", "x"), line(" " + repeat("─", in - 5) + "▶")}, "│ ↓2 ₑ │", "xₑ", "xe", "a", "a", "wₑ", "we", "│ ↑2 ₑ │", "──┐"),
        Row{line(lead), block("│      │", "SPLIT"), line(side), block("│  E(z)  │", "E"), line(gap), block("│  R(z)  │", "R"), line(side),
            block("│      │", "MERGE"), line("  "), block("⊕", "S"), line("──▶ "), label("x̂", "xh")},
        branch(Row{line(lead)}, "│ ↓2 ₒ │", "xₒ", "xo", "d", "d", "wₒ", "wo", "│ ↑2 ₒ │", "──┘"),
        lid("╰──────╯", "╰────────╯"),
    };
}

std::string heavy(const std::string& s) {
    static const std::pair<std::string_view, std::string_view> map[]{{"╭", "┏"}, {"╮", "┓"}, {"╰", "┗"}, {"╯", "┛"}, {"─", "━"}, {"│", "┃"}};
    std::string out;
    for (const auto& ch : split_codepoints(s)) {
        std::string_view c = ch;
        for (const auto& [from, to] : map)
            if (c == from) c = to;
        out += c;
    }
    return out;
}

std::string ascii(const std::string& s) {
    static const std::pair<std::string_view, std::string_view> map[]{
        {"╭", "+"}, {"╮", "+"}, {"╰", "+"}, {"╯", "+"}, {"┌", "+"}, {"└", "+"}, {"┐", "+"}, {"┘", "+"}, {"┤", "+"}, {"─", "-"},
        {"│", "|"}, {"▶", ">"}, {"⊕", "+"}, {"↓", "v"}, {"↑", "^"}, {"┏", "#"}, {"┓", "#"}, {"┗", "#"}, {"┛", "#"}, {"━", "="}, {"┃", "#"},
        {"ₑ", "e"}, {"ₒ", "o"}};
    std::string out;
    for (const auto& ch : split_codepoints(s)) {
        std::string_view c = ch;
        for (const auto& [from, to] : map)
            if (c == from) c = to;
        out += c;
    }
    return out;
}

}  // namespace

ftxui::Element bank_diagram(const DiagramSpec& spec, const UiContext& ui, std::map<std::string, ftxui::Box>* boxes, DiagramHits* hits) {
    using namespace ftxui;
    const auto& th = ui.theme;
    const bool plain = ui.caps.glyphs == GlyphMode::ascii;
    Elements lines;
    for (const auto& row : spec.polyphase ? polyphase_layout(spec) : direct_layout(spec)) {
        Elements parts;
        for (const auto& seg : row) {
            const bool on = seg.id && seg.kind == Kind::block && spec.highlight.contains(seg.id);
            const bool chosen = seg.id && seg.kind != Kind::block && !spec.selected.empty() && spec.selected == seg.id;
            std::string text = on ? heavy(seg.text) : seg.text;
            if (plain) text = ascii(text);
            const auto colour = on || chosen ? th.accent : seg.kind == Kind::line ? th.dim : th.fg;
            auto e = ftxui::text(text) | fg(colour, ui);
            if (on || chosen) e = e | bold;
            if (boxes && seg.id) {
                auto& box = (*boxes)[seg.id];
                e = e | reflect(box);
            }
            if (hits && seg.id) {
                hits->emplace_back(seg.id, ftxui::Box{});
                e = e | reflect(hits->back().second);
            }
            parts.push_back(std::move(e));
        }
        lines.push_back(hbox(std::move(parts)));
    }
    return vbox(std::move(lines));
}

}  // namespace signals::tui
