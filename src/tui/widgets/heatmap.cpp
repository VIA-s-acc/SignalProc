#include "tui/widgets/heatmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace signals::tui {
namespace {

class HeatmapNode : public ftxui::Node {
public:
    HeatmapNode(HeatmapSpec spec, Theme theme, ColorDepth depth, GlyphMode glyphs)
        : spec_{std::move(spec)}, theme_{std::move(theme)}, depth_{depth}, glyphs_{glyphs} {}

    void ComputeRequirement() override {
        requirement_.min_x = requirement_.min_y = 1;
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int W = box_.x_max - box_.x_min + 1, H = box_.y_max - box_.y_min + 1;
        if (W <= 0 || H <= 0 || spec_.rows == 0 || spec_.cols == 0 || spec_.values.size() < spec_.rows * spec_.cols) return;
        static constexpr std::array<const char*, 5> shades{" ", "░", "▒", "▓", "█"};
        static constexpr std::array<const char*, 10> ascii{" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const auto top = level(cx, 2 * cy, W, H), bottom = level(cx, 2 * cy + 1, W, H);
                auto& cell = screen.CellAt(box_.x_min + cx, box_.y_min + cy);
                const bool shaded = glyphs_ == GlyphMode::ascii || depth_ == ColorDepth::ansi16 || depth_ == ColorDepth::none;
                if (shaded) {
                    const double t = top && bottom ? (*top + *bottom) / 2 : top ? *top : bottom ? *bottom : 0.0;
                    cell.character = glyphs_ == GlyphMode::ascii ? ascii[static_cast<std::size_t>(std::lround(t * 9))]
                                                                 : shades[static_cast<std::size_t>(std::lround(t * 4))];
                    cell.foreground_color = to_color(theme_.fg, depth_);
                    continue;
                }
                cell.character = "▀";
                cell.foreground_color = to_color(top ? viridis(*top) : theme_.bg, depth_);
                cell.background_color = to_color(bottom ? viridis(*bottom) : theme_.bg, depth_);
            }
    }

private:
    std::optional<double> level(int cx, int py, int W, int H) const {
        const auto row = std::min(spec_.rows - 1, static_cast<std::size_t>(py) * spec_.rows / static_cast<std::size_t>(2 * H));
        const auto col = std::min(spec_.cols - 1, static_cast<std::size_t>(cx) * spec_.cols / static_cast<std::size_t>(W));
        const double v = spec_.values[row * spec_.cols + col];
        if (!std::isfinite(v)) return std::nullopt;
        if (spec_.ceil <= spec_.floor) return 0.0;
        return std::clamp((v - spec_.floor) / (spec_.ceil - spec_.floor), 0.0, 1.0);
    }

    HeatmapSpec spec_;
    Theme theme_;
    ColorDepth depth_;
    GlyphMode glyphs_;
};

}  // namespace

ftxui::Element heatmap(HeatmapSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs) {
    return std::make_shared<HeatmapNode>(std::move(spec), theme, depth, glyphs);
}

}  // namespace signals::tui
