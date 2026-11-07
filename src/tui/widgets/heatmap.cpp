#include "tui/widgets/heatmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/widgets/screen_util.hpp"

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
        if (spec_.scale == HeatScale::diverging) return diverging(screen, W, H);
        static constexpr std::array<const char*, 5> shades{" ", "░", "▒", "▓", "█"};
        static constexpr std::array<const char*, 10> ascii{" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const auto top = level(cx, 2 * cy, W, H), bottom = level(cx, 2 * cy + 1, W, H);
                const bool top_shaded = shaded_at(cx, 2 * cy, W, H), bottom_shaded = shaded_at(cx, 2 * cy + 1, W, H);
                auto& cell = screen.CellAt(box_.x_min + cx, box_.y_min + cy);
                const bool shaded = glyphs_ == GlyphMode::ascii || depth_ == ColorDepth::ansi16 || depth_ == ColorDepth::none;
                if (shaded) {
                    const double t = top && bottom ? (*top + *bottom) / 2 : top ? *top : bottom ? *bottom : 0.0;
                    cell.character = glyphs_ == GlyphMode::ascii ? ascii[static_cast<std::size_t>(std::lround(t * 9))]
                                                                 : shades[static_cast<std::size_t>(std::lround(t * 4))];
                    cell.foreground_color = to_color(top_shaded ? theme_.dim : theme_.fg, depth_);
                    continue;
                }
                auto paint = [&](const std::optional<double>& v, bool dim) {
                    if (!v) return theme_.bg;
                    return dim ? tint(viridis(*v), theme_.bg, 0.5) : viridis(*v);
                };
                cell.character = "▀";
                cell.foreground_color = to_color(paint(top, top_shaded), depth_);
                cell.background_color = to_color(paint(bottom, bottom_shaded), depth_);
            }
    }

private:
    std::pair<std::size_t, std::size_t> cell(int cx, int py, int W, int H) const {
        const auto row = std::min(spec_.rows - 1, static_cast<std::size_t>(py) * spec_.rows / static_cast<std::size_t>(2 * H));
        const auto col = std::min(spec_.cols - 1, static_cast<std::size_t>(cx) * spec_.cols / static_cast<std::size_t>(W));
        return {row, col};
    }

    void diverging(ftxui::Screen& screen, int W, int H) {
        double limit = 0;
        for (double v : spec_.values)
            if (std::isfinite(v)) limit = std::max(limit, std::abs(v));
        const bool shaded = glyphs_ == GlyphMode::ascii || depth_ == ColorDepth::ansi16 || depth_ == ColorDepth::none;
        auto colour = [&](std::size_t row, std::size_t col) {
            const double v = spec_.values[row * spec_.cols + col];
            if (spec_.highlight_row == row && std::abs(v) <= spec_.zero) return highlight_color(theme_);
            return diverging_color(std::abs(v) <= spec_.zero ? 0.0 : v, limit, theme_);
        };
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const auto [r0, c0] = cell(cx, 2 * cy, W, H);
                const auto [r1, c1] = cell(cx, 2 * cy + 1, W, H);
                auto& out = screen.CellAt(box_.x_min + cx, box_.y_min + cy);
                if (shaded) {
                    const double v = spec_.values[r0 * spec_.cols + c0];
                    const bool zero = !std::isfinite(v) || std::abs(v) <= spec_.zero;
                    out.character = zero ? (glyphs_ == GlyphMode::ascii ? "." : "·") : v > 0 ? "+" : (glyphs_ == GlyphMode::ascii ? "-" : "−");
                    out.foreground_color = to_color(zero ? theme_.dim : v > 0 ? theme_.trace[0] : theme_.trace[1], depth_);
                    continue;
                }
                out.character = "▀";
                out.foreground_color = to_color(colour(r0, c0), depth_);
                out.background_color = to_color(colour(r1, c1), depth_);
            }
    }

    bool shaded_at(int cx, int py, int W, int H) const {
        if (spec_.shade.size() < spec_.rows * spec_.cols) return false;
        const auto [row, col] = cell(cx, py, W, H);
        return spec_.shade[row * spec_.cols + col] != 0;
    }

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

Rgb diverging_color(double v, double limit, const Theme& theme) {
    const Rgb zero = tint(theme.bg, theme.fg, 0.05);
    if (!(limit > 0) || !std::isfinite(v) || v == 0) return zero;
    const double t = std::clamp(std::abs(v) / limit, 0.0, 1.0);
    return tint(zero, v > 0 ? theme.trace[0] : theme.trace[1], t);
}

Rgb highlight_color(const Theme& theme) { return tint(theme.bg, theme.accent, 0.25); }

ftxui::Element heatmap(HeatmapSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs) {
    return std::make_shared<HeatmapNode>(std::move(spec), theme, depth, glyphs);
}

}  // namespace signals::tui
