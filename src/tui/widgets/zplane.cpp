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
        const double cxd = (DW - 1) / 2.0, cyd = (DH - 1) / 2.0;
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
            if (std::abs(re) > spec_.extent || std::abs(im) > spec_.extent) {
                outside += m.count;
                re = std::clamp(re, -spec_.extent, spec_.extent);
                im = std::clamp(im, -spec_.extent, spec_.extent);
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

}  // namespace signals::tui
