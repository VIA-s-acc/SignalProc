#include "tui/shell/panel.hpp"

#include <algorithm>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

class PanelNode : public ftxui::Node {
public:
    PanelNode(std::string title, std::vector<Chip> chips, ftxui::Element body, bool focused, const UiContext& ui, ftxui::Box* title_box)
        : ftxui::Node({std::move(body)}), title_{std::move(title)}, chips_{std::move(chips)}, focused_{focused},
          theme_{ui.theme}, depth_{ui.caps.color}, glyphs_{ui.caps.glyphs}, title_box_{title_box} {}

    void ComputeRequirement() override {
        children_[0]->ComputeRequirement();
        requirement_ = children_[0]->requirement();
        requirement_.min_x = std::max(requirement_.min_x + 4, static_cast<int>(split_codepoints(title_).size()) + 6);
        requirement_.min_y += 2;
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        ftxui::Box inner = box;
        inner.x_min += 2;
        inner.x_max -= 2;
        inner.y_min += 1;
        inner.y_max -= 1;
        children_[0]->SetBox(inner);
    }

    void Render(ftxui::Screen& screen) override {
        children_[0]->Render(screen);
        const Rgb border = focused_ ? theme_.accent : theme_.dim;
        const int x0 = box_.x_min, x1 = box_.x_max, y0 = box_.y_min, y1 = box_.y_max;
        if (x1 - x0 < 2 || y1 - y0 < 1) return;
        put(screen, x0, y0, glyph(Glyph::tl, glyphs_), border);
        put(screen, x1, y0, glyph(Glyph::tr, glyphs_), border);
        put(screen, x0, y1, glyph(Glyph::bl, glyphs_), border);
        put(screen, x1, y1, glyph(Glyph::br, glyphs_), border);
        for (int x = x0 + 1; x < x1; ++x) {
            put(screen, x, y0, glyph(Glyph::h, glyphs_), border);
            put(screen, x, y1, glyph(Glyph::h, glyphs_), border);
        }
        for (int y = y0 + 1; y < y1; ++y) {
            put(screen, x0, y, glyph(Glyph::v, glyphs_), border);
            put(screen, x1, y, glyph(Glyph::v, glyphs_), border);
        }
        const auto title = split_codepoints(" " + title_ + " ");
        int x = x0 + 2;
        for (const auto& ch : title) {
            if (x >= x1 - 1) break;
            put(screen, x, y0, ch, focused_ ? theme_.accent : theme_.fg, true);
            ++x;
        }
        if (title_box_) *title_box_ = {x0 + 2, x - 1, y0, y0};
        int width = 0;
        for (const auto& c : chips_) width += static_cast<int>(split_codepoints(c.on ? "[" + c.label + "]" : c.label).size()) + 1;
        int cx = x1 - 1 - width;
        if (chips_.empty() || cx <= x + 1) return;
        for (const auto& c : chips_) {
            put(screen, cx++, y0, " ", border);
            const int start = cx;
            for (const auto& ch : split_codepoints(c.on ? "[" + c.label + "]" : c.label)) put(screen, cx++, y0, ch, c.on ? theme_.accent : theme_.dim, c.on);
            if (c.box) *c.box = {start, cx - 1, y0, y0};
        }
    }

private:
    void put(ftxui::Screen& screen, int x, int y, std::string_view ch, Rgb c, bool bold = false) const {
        auto& cell = screen.CellAt(x, y);
        cell.character = std::string{ch};
        cell.foreground_color = to_color(c, depth_);
        cell.bold = bold;
    }

    std::string title_;
    std::vector<Chip> chips_;
    bool focused_;
    Theme theme_;
    ColorDepth depth_;
    GlyphMode glyphs_;
    ftxui::Box* title_box_;
};

}  // namespace

ftxui::Element panel(std::string_view title, std::vector<Chip> chips, ftxui::Element body, bool focused, const UiContext& ui,
                     ftxui::Box* title_box) {
    return std::make_shared<PanelNode>(std::string{title}, std::move(chips), std::move(body), focused, ui, title_box);
}

ftxui::Decorator fg(Rgb c, const UiContext& ui) { return ftxui::color(to_color(c, ui.caps.color)); }

ftxui::Decorator bg(Rgb c, const UiContext& ui) { return ftxui::bgcolor(to_color(c, ui.caps.color)); }

ftxui::Element key_label(std::string_view key, const UiContext& ui) {
    return ftxui::text(std::string{key}) | ftxui::bold | fg(ui.theme.bg, ui) | bg(ui.theme.dim, ui);
}

}  // namespace signals::tui
