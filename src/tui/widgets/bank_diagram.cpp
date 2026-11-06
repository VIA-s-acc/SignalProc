#include "tui/widgets/bank_diagram.hpp"

#include <string_view>
#include <utility>
#include <vector>

#include "tui/shell/panel.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

struct Segment {
    std::string text;
    const char* id;
};

std::vector<std::vector<Segment>> layout(const DiagramSpec& s) {
    const std::string down = "│ " + s.down + " │", up = "│ " + s.up + " │";
    return {
        {{"              ", nullptr}, {"╭────╮", "H0"}, {"      ", nullptr}, {"╭────╮", "D0"}, {"                        ", nullptr},
         {"╭────╮", "U0"}, {"      ", nullptr}, {"╭────╮", "F0"}},
        {{"           ┌─▶", nullptr}, {"│ H₀ │", "H0"}, {"─", nullptr}, {" y₀ ", "y0"}, {"▶", nullptr}, {down, "D0"}, {"─────── ", nullptr},
         {"a", "a"}, {" ─────────────▶", nullptr}, {up, "U0"}, {"─", nullptr}, {" u₀ ", "u0"}, {"▶", nullptr}, {"│ F₀ │", "F0"}, {"──┐", nullptr}},
        {{"  ", nullptr}, {"x", "x"}, {" ───────┤  ", nullptr}, {"╰────╯", "H0"}, {"      ", nullptr}, {"╰────╯", "D0"},
         {"                        ", nullptr}, {"╰────╯", "U0"}, {"      ", nullptr}, {"╰────╯", "F0"}, {"  │", nullptr}},
        {{"           │                                                                ", nullptr}, {"⊕", "S"}, {"──▶ ", nullptr}, {"x̂", "xh"}},
        {{"           │  ", nullptr}, {"╭────╮", "H1"}, {"      ", nullptr}, {"╭────╮", "D1"}, {"                        ", nullptr},
         {"╭────╮", "U1"}, {"      ", nullptr}, {"╭────╮", "F1"}, {"  │", nullptr}},
        {{"           └─▶", nullptr}, {"│ H₁ │", "H1"}, {"─", nullptr}, {" y₁ ", "y1"}, {"▶", nullptr}, {down, "D1"}, {"─────── ", nullptr},
         {"d", "d"}, {" ─────────────▶", nullptr}, {up, "U1"}, {"─", nullptr}, {" u₁ ", "u1"}, {"▶", nullptr}, {"│ F₁ │", "F1"}, {"──┘", nullptr}},
        {{"              ", nullptr}, {"╰────╯", "H1"}, {"      ", nullptr}, {"╰────╯", "D1"}, {"                        ", nullptr},
         {"╰────╯", "U1"}, {"      ", nullptr}, {"╰────╯", "F1"}},
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
        {"│", "|"}, {"▶", ">"}, {"⊕", "+"}, {"↓", "v"}, {"↑", "^"}, {"┏", "#"}, {"┓", "#"}, {"┗", "#"}, {"┛", "#"}, {"━", "="}, {"┃", "#"}};
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

ftxui::Element bank_diagram(const DiagramSpec& spec, const UiContext& ui, std::map<std::string, ftxui::Box>* boxes) {
    using namespace ftxui;
    const auto& th = ui.theme;
    const bool plain = ui.caps.glyphs == GlyphMode::ascii;
    Elements lines;
    for (const auto& row : layout(spec)) {
        Elements parts;
        for (const auto& seg : row) {
            const bool on = seg.id && spec.highlight.contains(seg.id);
            const bool block = seg.id && std::string_view{seg.id}.size() == 2 && seg.id[0] != 'y' && seg.id[0] != 'u' && seg.id[0] != 'x';
            std::string text = on && block ? heavy(seg.text) : seg.text;
            if (plain) text = ascii(text);
            auto e = ftxui::text(text) | fg(on ? th.accent : block ? th.fg : seg.id ? th.fg : th.dim, ui);
            if (on) e = e | bold;
            if (boxes && seg.id) {
                auto& box = (*boxes)[seg.id];
                e = e | reflect(box);
            }
            parts.push_back(std::move(e));
        }
        lines.push_back(hbox(std::move(parts)));
    }
    return vbox(std::move(lines));
}

}  // namespace signals::tui
