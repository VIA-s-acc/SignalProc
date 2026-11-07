#include "tui/widgets/zplane.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "i18n/i18n.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr double kSameRoot = 1e-4;

struct Marker {
    std::complex<double> z;
    int count;
};

std::vector<Marker> group(const std::vector<std::complex<double>>& roots) {
    std::vector<Marker> out;
    for (const auto& r : roots) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Marker& m) { return std::abs(m.z - r) < kSameRoot; });
        if (it == out.end()) out.push_back({r, 1});
        else ++it->count;
    }
    return out;
}

class ZPlaneNode : public ftxui::Node {
public:
    ZPlaneNode(ZPlaneSpec spec, Theme theme, ColorDepth depth, GlyphMode glyphs)
        : spec_{std::move(spec)}, theme_{std::move(theme)}, depth_{depth}, glyphs_{glyphs} {}

    void ComputeRequirement() override {
        requirement_.min_x = requirement_.min_y = 1;
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int W = box_.x_max - box_.x_min + 1, H = box_.y_max - box_.y_min + 1;
        if (W <= 0 || H <= 0) return;
        const int DW = 2 * W, DH = 4 * H, side = std::min(DW, DH);
        const double scale = (side - 1) / (2 * spec_.extent);
        const double mx = (DW - 1) / 2.0, my = (DH - 1) / 2.0;
        const double cxd = mx - scale * spec_.center.real(), cyd = my + scale * spec_.center.imag();
        const double re_lo = spec_.center.real() - spec_.extent, re_hi = spec_.center.real() + spec_.extent;
        const double im_lo = spec_.center.imag() - spec_.extent, im_hi = spec_.center.imag() + spec_.extent;
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(W * H), 0);
        auto set = [&](double dx, double dy) {
            const int x = static_cast<int>(std::lround(dx)), y = static_cast<int>(std::lround(dy));
            if (x < 0 || y < 0 || x >= DW || y >= DH) return;
            const int bit = y % 4 < 3 ? (x % 2 == 0 ? 1 << (y % 4) : 1 << (y % 4 + 3)) : (x % 2 == 0 ? 0x40 : 0x80);
            mask[static_cast<std::size_t>((y / 4) * W + x / 2)] |= static_cast<std::uint8_t>(bit);
        };
        for (int x = 0; x < DW; x += 2) set(x, cyd);
        for (int y = 0; y < DH; y += 2) set(cxd, y);
        const int steps = 8 * side;
        for (int k = 0; k < steps; ++k) {
            const double a = 2 * std::numbers::pi * k / steps;
            set(cxd + scale * std::cos(a), cyd - scale * std::sin(a));
        }
        for (int cy = 0; cy < H; ++cy)
            for (int cx = 0; cx < W; ++cx) {
                const auto m = mask[static_cast<std::size_t>(cy * W + cx)];
                if (!m) continue;
                auto& cell = screen.CellAt(box_.x_min + cx, box_.y_min + cy);
                cell.character = glyphs_ == GlyphMode::ascii ? "." : utf8(0x2800 + m);
                cell.foreground_color = to_color(theme_.dim, depth_);
            }
        int outside = 0;
        auto place = [&](const Marker& m, const char* uni, const char* asc, Rgb colour) {
            double re = m.z.real(), im = m.z.imag();
            if (re < re_lo || re > re_hi || im < im_lo || im > im_hi) {
                outside += m.count;
                re = std::clamp(re, re_lo, re_hi);
                im = std::clamp(im, im_lo, im_hi);
            }
            const int x = box_.x_min + std::clamp(static_cast<int>(std::lround(cxd + scale * re)) / 2, 0, W - 1);
            const int y = box_.y_min + std::clamp(static_cast<int>(std::lround(cyd - scale * im)) / 4, 0, H - 1);
            put(screen, x, y, glyphs_ == GlyphMode::ascii ? asc : uni, colour);
            if (m.count > 1)
                put(screen, x + 1, y, glyphs_ == GlyphMode::ascii ? std::to_string(m.count) : superscript_count(m.count), colour);
        };
        for (const auto& m : group(spec_.zeros)) place(m, "○", "o", theme_.trace[0]);
        auto poles = spec_.poles;
        for (std::ptrdiff_t k = 0; k < spec_.poles_at_origin; ++k) poles.emplace_back(0.0, 0.0);
        for (const auto& m : group(poles)) place(m, "×", "x", theme_.trace[1]);
        if (spec_.hits) spec_.hits->clear();
        for (const auto& m : spec_.markers) {
            double re = m.z.real(), im = m.z.imag();
            re = std::clamp(re, re_lo, re_hi);
            im = std::clamp(im, im_lo, im_hi);
            const int x = box_.x_min + std::clamp(static_cast<int>(std::lround(cxd + scale * re)) / 2, 0, W - 1);
            const int y = box_.y_min + std::clamp(static_cast<int>(std::lround(cyd - scale * im)) / 4, 0, H - 1);
            put(screen, x, y, glyphs_ == GlyphMode::ascii ? (m.glyph == "●" ? "*" : m.glyph == "◉" ? "@" : "o") : m.glyph, m.color);
            if (spec_.hits && m.id >= 0) spec_.hits->push_back({m.id, x, y});
            if (m.id < 0 && spec_.minus_one_label) {
                const auto label = split_codepoints(*spec_.minus_one_label);
                const int lx = std::max(box_.x_min, x - static_cast<int>(label.size()) / 2);
                for (std::size_t k = 0; k < label.size(); ++k) put(screen, lx + static_cast<int>(k), y + 1, label[k], theme_.dim);
            }
        }
        if (outside > 0) {
            const auto label = split_codepoints(tr("tui.zplane.outside") + std::to_string(outside));
            for (std::size_t k = 0; k < label.size(); ++k) put(screen, box_.x_min + static_cast<int>(k), box_.y_min, label[k], theme_.warn);
        }
    }

private:
    void put(ftxui::Screen& screen, int x, int y, const std::string& ch, Rgb c) const {
        if (x < box_.x_min || x > box_.x_max || y < box_.y_min || y > box_.y_max) return;
        auto& cell = screen.CellAt(x, y);
        cell.character = ch;
        cell.foreground_color = to_color(c, depth_);
    }

    ZPlaneSpec spec_;
    Theme theme_;
    ColorDepth depth_;
    GlyphMode glyphs_;
};

}  // namespace

ftxui::Element zplane(ZPlaneSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs) {
    return std::make_shared<ZPlaneNode>(std::move(spec), theme, depth, glyphs);
}

namespace {

constexpr double kZStep = 1.25, kZMax = 50.0;

double dot_scale(const ftxui::Box& box, double extent) {
    const int W = box.x_max - box.x_min + 1, H = box.y_max - box.y_min + 1;
    return (std::max(1, std::min(2 * W, 4 * H)) - 1) / (2 * extent);
}

}  // namespace

std::complex<double> ZView::at(const ftxui::Box& box, int x, int y, double base) const {
    const double scale = dot_scale(box, extent(base));
    const int W = box.x_max - box.x_min + 1, H = box.y_max - box.y_min + 1;
    const double dx = 2.0 * (x - box.x_min) + 0.5 - (2 * W - 1) / 2.0, dy = 4.0 * (y - box.y_min) + 1.5 - (4 * H - 1) / 2.0;
    return center_ + std::complex<double>{dx / scale, -dy / scale};
}

void ZView::zoom_by(double factor, std::optional<std::complex<double>> anchor, double base) {
    const double next = std::clamp(zoom_ * factor, 1.0, kZMax);
    if (next == 1.0) {
        reset();
        return;
    }
    if (anchor) center_ = *anchor - (*anchor - center_) * (zoom_ / next);
    zoom_ = next;
    const double limit = 2 * base;
    center_ = {std::clamp(center_.real(), -limit, limit), std::clamp(center_.imag(), -limit, limit)};
}

bool ZView::on_mouse(const ftxui::Mouse& m, const ftxui::Box& box, double base) {
    using ftxui::Mouse;
    const bool inside = box.x_max >= box.x_min && box.Contain(m.x, m.y);
    if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && inside) {
        zoom_by(m.button == Mouse::WheelUp ? kZStep : 1 / kZStep, at(box, m.x, m.y, base), base);
        return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed && inside) {
        dragging_ = true;
        last_x_ = m.x, last_y_ = m.y;
        return true;
    }
    if (!dragging_) return false;
    if (m.motion == Mouse::Released) {
        dragging_ = false;
        return true;
    }
    const double scale = dot_scale(box, extent(base));
    center_ -= std::complex<double>{2.0 * (m.x - last_x_) / scale, -4.0 * (m.y - last_y_) / scale};
    last_x_ = m.x, last_y_ = m.y;
    return true;
}

bool ZView::on_key(const ftxui::Event& e, double base) {
    using ftxui::Event;
    if (e == Event::Character('+') || e == Event::Character('=')) return zoom_by(kZStep, std::nullopt, base), true;
    if (e == Event::Character('-')) return zoom_by(1 / kZStep, std::nullopt, base), true;
    if (e == Event::Character('0')) return reset(), true;
    const double step = 0.2 * extent(base);
    if (e == Event::ArrowLeftCtrl) return center_ -= step, true;
    if (e == Event::ArrowRightCtrl) return center_ += step, true;
    if (e == Event::ArrowUpCtrl) return center_ += std::complex<double>{0, step}, true;
    if (e == Event::ArrowDownCtrl) return center_ -= std::complex<double>{0, step}, true;
    return false;
}

}  // namespace signals::tui
