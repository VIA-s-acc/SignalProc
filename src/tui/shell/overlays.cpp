#include "tui/shell/overlays.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr std::string_view kAlphabet = "asdfghjklqwertyuiopzxcvbnm";

class PaintNode : public ftxui::Node {
public:
    explicit PaintNode(std::function<void(ftxui::Screen&)> paint) : paint_{std::move(paint)} {}
    void ComputeRequirement() override { requirement_ = {}; }
    void Render(ftxui::Screen& screen) override { paint_(screen); }

private:
    std::function<void(ftxui::Screen&)> paint_;
};

void put(ftxui::Screen& screen, int x, int y, const std::string& ch, ftxui::Color fg, ftxui::Color bg, bool bold = false) {
    if (x < 0 || y < 0 || x >= screen.dimx() || y >= screen.dimy()) return;
    auto& cell = screen.CellAt(x, y);
    cell.character = ch;
    cell.foreground_color = fg;
    cell.background_color = bg;
    cell.bold = bold;
}

void put_text(ftxui::Screen& screen, int x, int y, const std::string& text, ftxui::Color fg, ftxui::Color bg, bool bold = false) {
    for (const auto& ch : split_codepoints(text)) put(screen, x++, y, ch, fg, bg, bold);
}

}  // namespace

std::vector<std::string> jump_labels(std::size_t count) {
    std::vector<std::string> out;
    if (count <= kAlphabet.size()) {
        for (std::size_t i = 0; i < count; ++i) out.emplace_back(1, kAlphabet[i]);
        return out;
    }
    for (char a : kAlphabet)
        for (char b : kAlphabet) {
            if (out.size() == count) return out;
            out.push_back(std::string{a, b});
        }
    return out;
}

void ContextMenu::open(std::vector<MenuItem> items, int x, int y) {
    items_ = std::move(items);
    x_ = x;
    y_ = y;
    selected_ = 0;
    while (selected_ < items_.size() && !items_[selected_].enabled) ++selected_;
    rows_.clear();
    frame_ = {};
    open_ = !items_.empty();
}

ftxui::Element ContextMenu::render(const UiContext& ui, int width, int height) {
    int label_w = 0, key_w = 0;
    for (const auto& item : items_) {
        label_w = std::max(label_w, display_width(item.label));
        key_w = std::max(key_w, display_width(item.key));
    }
    const int inner = label_w + (key_w ? key_w + 2 : 0) + 2;
    const int w = inner + 2, h = static_cast<int>(items_.size()) + 2;
    int left = std::clamp(x_, 0, std::max(0, width - w));
    int top = y_ + 1 + h <= height ? y_ + 1 : std::max(0, y_ - h);
    frame_ = {left, left + w - 1, top, top + h - 1};
    rows_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int y = top + 1 + static_cast<int>(i);
        rows_.push_back({left + 1, left + w - 2, y, y});
    }
    const auto depth = ui.caps.color;
    const auto th = ui.theme;
    const auto glyphs = ui.caps.glyphs;
    return std::make_shared<PaintNode>([this, depth, th, glyphs, label_w, inner](ftxui::Screen& screen) {
        const auto border = to_color(th.accent, depth), back = to_color(th.bg, depth);
        const int x0 = frame_.x_min, x1 = frame_.x_max, y0 = frame_.y_min, y1 = frame_.y_max;
        put(screen, x0, y0, std::string{glyph(Glyph::tl, glyphs)}, border, back);
        put(screen, x1, y0, std::string{glyph(Glyph::tr, glyphs)}, border, back);
        put(screen, x0, y1, std::string{glyph(Glyph::bl, glyphs)}, border, back);
        put(screen, x1, y1, std::string{glyph(Glyph::br, glyphs)}, border, back);
        for (int x = x0 + 1; x < x1; ++x) {
            put(screen, x, y0, std::string{glyph(Glyph::h, glyphs)}, border, back);
            put(screen, x, y1, std::string{glyph(Glyph::h, glyphs)}, border, back);
        }
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const int y = y0 + 1 + static_cast<int>(i);
            const bool on = i == selected_ && items_[i].enabled;
            const auto fg = to_color(on ? th.bg : items_[i].enabled ? th.fg : th.dim, depth);
            const auto bg = on ? to_color(th.accent, depth) : back;
            put(screen, x0, y, std::string{glyph(Glyph::v, glyphs)}, border, back);
            put(screen, x1, y, std::string{glyph(Glyph::v, glyphs)}, border, back);
            const std::string row = " " + pad_right(items_[i].label, label_w) +
                                    (items_[i].key.empty() ? std::string{} : "  " + items_[i].key);
            put_text(screen, x0 + 1, y, pad_right(row, inner), fg, bg, on);
        }
    });
}

void ContextMenu::activate(std::size_t index) {
    if (index >= items_.size() || !items_[index].enabled) return;
    auto action = items_[index].action;
    open_ = false;
    if (action) action();
}

void ContextMenu::move(int direction) {
    if (items_.empty()) return;
    const auto n = items_.size();
    auto i = selected_;
    for (std::size_t step = 0; step < n; ++step) {
        i = direction > 0 ? (i + 1) % n : (i + n - 1) % n;
        if (items_[i].enabled) {
            selected_ = i;
            return;
        }
    }
}

bool ContextMenu::on_event(ftxui::Event e) {
    using ftxui::Event;
    using ftxui::Mouse;
    if (e == Event::Escape) {
        open_ = false;
        return true;
    }
    if (e == Event::ArrowDown || e == Event::Tab) {
        move(1);
        return true;
    }
    if (e == Event::ArrowUp || e == Event::TabReverse) {
        move(-1);
        return true;
    }
    if (e == Event::Return) {
        activate(selected_);
        return true;
    }
    if (!e.is_mouse()) return true;
    const auto& m = e.mouse();
    std::optional<std::size_t> row;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].Contain(m.x, m.y)) row = i;
    if (m.motion == Mouse::Moved && row && items_[*row].enabled) selected_ = *row;
    if (m.motion != Mouse::Pressed || (m.button != Mouse::Left && m.button != Mouse::Right)) return true;
    if (row) activate(*row);
    else if (!frame_.Contain(m.x, m.y)) open_ = false;
    return true;
}

void JumpMode::start(std::vector<JumpTarget> targets) {
    std::erase_if(targets, [](const JumpTarget& t) { return t.box.x_max < t.box.x_min || t.box.y_max < t.box.y_min; });
    targets_ = std::move(targets);
    labels_ = jump_labels(targets_.size());
    targets_.resize(labels_.size());
    typed_.clear();
    active_ = !targets_.empty();
}

void JumpMode::cancel() {
    active_ = false;
    typed_.clear();
}

ftxui::Element JumpMode::render(const UiContext& ui) const {
    const auto depth = ui.caps.color;
    const auto th = ui.theme;
    return std::make_shared<PaintNode>([this, depth, th](ftxui::Screen& screen) {
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            if (!labels_[i].starts_with(typed_)) continue;
            put_text(screen, targets_[i].box.x_min, targets_[i].box.y_min, labels_[i], to_color(th.bg, depth), to_color(th.accent, depth), true);
        }
    });
}

std::optional<ftxui::Box> JumpMode::on_key(std::string_view ch) {
    typed_ += ch;
    bool prefix = false;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        if (labels_[i] == typed_) {
            cancel();
            return targets_[i].box;
        }
        if (labels_[i].starts_with(typed_)) prefix = true;
    }
    if (!prefix) cancel();
    return std::nullopt;
}

}  // namespace signals::tui
