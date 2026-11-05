#include "tui/widgets/plot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/widgets/format.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kYLabelWidth = 6;

Rgb blend(Rgb a, Rgb b, double t) {
    auto mix = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x + (y - x) * t)); };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

std::uint8_t dot_bit(int col, int row) {
    if (row < 3) return static_cast<std::uint8_t>(col == 0 ? 1 << row : 1 << (row + 3));
    return static_cast<std::uint8_t>(col == 0 ? 0x40 : 0x80);
}

class Raster {
public:
    Raster(int w, int h) : w_{w}, h_{h}, mask_(static_cast<std::size_t>(w * h), 0), color_(static_cast<std::size_t>(w * h)) {}
    int dots_w() const { return w_ * 2; }
    int dots_h() const { return h_ * 4; }
    void set(int dx, int dy, Rgb c) {
        if (dx < 0 || dy < 0 || dx >= dots_w() || dy >= dots_h()) return;
        const auto i = static_cast<std::size_t>((dy / 4) * w_ + dx / 2);
        mask_[i] |= dot_bit(dx % 2, dy % 4);
        color_[i] = c;
    }
    std::uint8_t mask(int cx, int cy) const { return mask_[static_cast<std::size_t>(cy * w_ + cx)]; }
    Rgb color(int cx, int cy) const { return color_[static_cast<std::size_t>(cy * w_ + cx)]; }

private:
    int w_, h_;
    std::vector<std::uint8_t> mask_;
    std::vector<Rgb> color_;
};

bool keep(LineStyle s, int dx) {
    switch (s) {
    case LineStyle::solid: return true;
    case LineStyle::dashed: return (dx / 2) % 2 == 0;
    case LineStyle::dotted: return dx % 4 == 0;
    }
    return true;
}

void line(Raster& r, int x0, int y0, int x1, int y1, Rgb c, LineStyle s) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (keep(s, x0)) r.set(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
}

class PlotNode : public ftxui::Node {
public:
    PlotNode(PlotSpec spec, Theme theme, ColorDepth depth, GlyphMode glyphs)
        : spec_{std::move(spec)}, theme_{std::move(theme)}, depth_{depth}, glyphs_{glyphs} {}

    void ComputeRequirement() override {
        requirement_.min_x = 1;
        requirement_.min_y = 1;
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int W = box_.x_max - box_.x_min + 1, H = box_.y_max - box_.y_min + 1;
        if (W <= 0 || H <= 0) return;
        if (H < 3) return sparkline(screen, W);
        const bool labels = spec_.labels && H >= 6 && W >= 20;
        const int x0 = box_.x_min + (labels ? kYLabelWidth : 0);
        const int pw = box_.x_max - x0 + 1, ph = H - (labels ? 1 : 0);
        if (pw <= 0 || ph <= 0) return;
        Raster r{pw, ph};
        const int DW = r.dots_w(), DH = r.dots_h();
        auto xdot = [&](double v) { return static_cast<int>(std::lround(axis_fraction(spec_.x, v) * (DW - 1))); };
        auto ydot = [&](double v) { return static_cast<int>(std::lround((1 - axis_fraction(spec_.y, v)) * (DH - 1))); };

        if (spec_.selection) {
            const auto [a, b] = *spec_.selection;
            const int c0 = column_of_value(spec_.x, std::min(a, b), pw), c1 = column_of_value(spec_.x, std::max(a, b), pw);
            const auto bg = to_color(blend(theme_.bg, theme_.accent, 0.15), depth_);
            for (int cy = 0; cy < ph; ++cy)
                for (int cx = c0; cx <= c1; ++cx) screen.CellAt(x0 + cx, box_.y_min + cy).background_color = bg;
        }
        const auto xticks = ticks(spec_.x, std::max(2, pw / 8));
        const auto yticks = ticks(spec_.y, std::max(2, ph / 3));
        if (labels) {
            for (const auto& t : yticks) {
                const int dy = ydot(t.value);
                for (int dx = 0; dx < DW; dx += 3) r.set(dx, dy, theme_.dim);
            }
            for (const auto& t : xticks) {
                const int dx = xdot(t.value);
                for (int dy = 0; dy < DH; dy += 3) r.set(dx, dy, theme_.dim);
            }
        }
        for (double m : spec_.x_marks) {
            const int dx = xdot(m);
            for (int dy = 0; dy < DH; dy += 2) r.set(dx, dy, theme_.dim);
        }
        for (double m : spec_.y_marks) {
            const int dy = ydot(m);
            for (int dx = 0; dx < DW; dx += 2) r.set(dx, dy, theme_.dim);
        }
        for (const auto& e : spec_.envelopes) envelope(r, e, ydot);
        for (bool focused : {false, true})
            for (const auto& t : spec_.traces)
                if (t.focused == focused) trace(r, t, xdot, ydot);

        for (int cy = 0; cy < ph; ++cy)
            for (int cx = 0; cx < pw; ++cx) {
                const auto m = r.mask(cx, cy);
                if (!m) continue;
                auto& cell = screen.CellAt(x0 + cx, box_.y_min + cy);
                cell.character = glyphs_ == GlyphMode::ascii ? "*" : utf8(0x2800 + m);
                cell.foreground_color = to_color(r.color(cx, cy), depth_);
            }
        cursor(screen, spec_.cursor_b, theme_.fg, x0, pw, ph);
        cursor(screen, spec_.cursor_a, theme_.accent, x0, pw, ph);
        if (labels) {
            int last_row = -10;
            for (const auto& t : yticks) {
                const int row = std::clamp(ydot(t.value) / 4, 0, ph - 1);
                if (std::abs(row - last_row) < 1) continue;
                last_row = row;
                write_right(screen, t.label, x0 - 2, box_.y_min + row);
            }
            int next_free = x0;
            for (const auto& t : xticks) {
                const auto cps = split_codepoints(t.label);
                int start = x0 + std::clamp(xdot(t.value) / 2 - static_cast<int>(cps.size()) / 2, 0, std::max(0, pw - static_cast<int>(cps.size())));
                if (start < next_free) continue;
                for (std::size_t k = 0; k < cps.size(); ++k) put(screen, start + static_cast<int>(k), box_.y_max, cps[k], theme_.dim);
                next_free = start + static_cast<int>(cps.size()) + 1;
            }
        }
    }

private:
    std::vector<Tick> ticks(const Axis& a, int n) const {
        switch (a.scale) {
        case AxisScale::log10: return log_ticks(a.lo, a.hi, n);
        case AxisScale::pi: return pi_ticks(a.lo, a.hi, n);
        case AxisScale::linear: return nice_ticks(a.lo, a.hi, n);
        }
        return {};
    }

    template <class Y>
    void envelope(Raster& r, const Envelope& e, Y ydot) const {
        if (e.columns.empty()) return;
        const int DW = r.dots_w();
        int ptop = -1, pbot = -1;
        for (int dx = 0; dx < DW; ++dx) {
            const auto& c = e.columns[static_cast<std::size_t>(dx) * e.columns.size() / static_cast<std::size_t>(DW)];
            int top = ydot(c.hi), bot = ydot(c.lo);
            if (top > bot) std::swap(top, bot);
            if (ptop >= 0) {
                if (bot < ptop) bot = ptop;
                if (top > pbot) top = pbot;
            }
            for (int dy = top; dy <= bot; ++dy) r.set(dx, dy, e.color);
            ptop = top, pbot = bot;
        }
    }

    template <class X, class Y>
    void trace(Raster& r, const Trace& t, X xdot, Y ydot) const {
        const std::size_t n = t.y.size();
        if (n == 0) return;
        auto xv = [&](std::size_t i) {
            if (!t.x.empty()) return t.x[i];
            return n == 1 ? (spec_.x.lo + spec_.x.hi) / 2 : spec_.x.lo + (spec_.x.hi - spec_.x.lo) * static_cast<double>(i) / static_cast<double>(n - 1);
        };
        const int DH = r.dots_h();
        auto clampy = [&](int y) { return std::clamp(y, 0, DH - 1); };
        if (t.stems) {
            const int base = clampy(ydot(0.0));
            for (std::size_t i = 0; i < n; ++i)
                if (std::isfinite(t.y[i])) line(r, xdot(xv(i)), base, xdot(xv(i)), clampy(ydot(t.y[i])), t.color, LineStyle::solid);
            return;
        }
        bool have = false;
        int px = 0, py = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(t.y[i])) {
                have = false;
                continue;
            }
            const int x = xdot(xv(i)), y = clampy(ydot(t.y[i]));
            if (have) line(r, px, py, x, y, t.color, t.style);
            else if (keep(t.style, x)) r.set(x, y, t.color);
            px = x, py = y, have = true;
        }
    }

    void cursor(ftxui::Screen& screen, const std::optional<double>& at, Rgb c, int x0, int pw, int ph) const {
        if (!at) return;
        const int cx = x0 + column_of_value(spec_.x, *at, pw);
        for (int cy = 0; cy < ph; ++cy) put(screen, cx, box_.y_min + cy, glyphs_ == GlyphMode::ascii ? "|" : "│", c);
    }

    void put(ftxui::Screen& screen, int x, int y, const std::string& ch, Rgb c) const {
        if (x < box_.x_min || x > box_.x_max || y < box_.y_min || y > box_.y_max) return;
        auto& cell = screen.CellAt(x, y);
        cell.character = ch;
        cell.foreground_color = to_color(c, depth_);
    }

    void write_right(ftxui::Screen& screen, const std::string& label, int right, int y) const {
        const auto cps = split_codepoints(label);
        const int n = static_cast<int>(cps.size());
        for (int k = 0; k < n; ++k) put(screen, right - n + 1 + k, y, cps[static_cast<std::size_t>(k)], theme_.dim);
    }

    void sparkline(ftxui::Screen& screen, int W) {
        static constexpr std::array<char32_t, 8> levels{U'▁', U'▂', U'▃', U'▄', U'▅', U'▆', U'▇', U'█'};
        for (int cx = 0; cx < W; ++cx) {
            double v = std::nan("");
            if (!spec_.traces.empty() && !spec_.traces.front().y.empty()) {
                const auto& y = spec_.traces.front().y;
                v = y[std::min(y.size() - 1, static_cast<std::size_t>(cx) * y.size() / static_cast<std::size_t>(W))];
            } else if (!spec_.envelopes.empty() && !spec_.envelopes.front().columns.empty()) {
                const auto& c = spec_.envelopes.front().columns;
                v = c[std::min(c.size() - 1, static_cast<std::size_t>(cx) * c.size() / static_cast<std::size_t>(W))].hi;
            }
            if (!std::isfinite(v)) continue;
            const double f = std::clamp(axis_fraction(spec_.y, v), 0.0, 1.0);
            const auto idx = static_cast<std::size_t>(std::min(7.0, std::floor(f * 7.999)));
            put(screen, box_.x_min + cx, box_.y_max, glyphs_ == GlyphMode::ascii ? "*" : utf8(levels[idx]), theme_.trace[0]);
        }
    }

    PlotSpec spec_;
    Theme theme_;
    ColorDepth depth_;
    GlyphMode glyphs_;
};

}  // namespace

double axis_fraction(const Axis& a, double v) {
    if (!std::isfinite(v)) return 0.0;
    if (a.scale == AxisScale::log10) {
        if (v <= 0 || a.lo <= 0 || a.hi <= a.lo) return 0.0;
        return std::log(v / a.lo) / std::log(a.hi / a.lo);
    }
    if (a.hi == a.lo) return 0.0;
    return (v - a.lo) / (a.hi - a.lo);
}

double value_at_column(const Axis& a, int column, int columns) {
    if (columns <= 0) return a.lo;
    const double f = (std::clamp(column, 0, columns - 1) + 0.5) / columns;
    if (a.scale == AxisScale::log10 && a.lo > 0 && a.hi > a.lo) return a.lo * std::pow(a.hi / a.lo, f);
    return a.lo + f * (a.hi - a.lo);
}

int column_of_value(const Axis& a, double v, int columns) {
    if (columns <= 0) return 0;
    const double f = axis_fraction(a, v);
    return std::clamp(static_cast<int>(std::floor(f * columns)), 0, columns - 1);
}

ftxui::Element plot(PlotSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs) {
    return std::make_shared<PlotNode>(std::move(spec), theme, depth, glyphs);
}

}  // namespace signals::tui
