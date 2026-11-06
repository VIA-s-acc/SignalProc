#include "tui/widgets/screen_util.hpp"

#include <algorithm>
#include <cmath>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {

std::string group(std::uint64_t n) {
    const auto digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += language() == Language::en ? ',' : ' ';
        out += digits[i];
    }
    return out;
}

std::string hz_text(double f) {
    if (f >= 1000) return format_fixed(f / 1000, f >= 10000 ? 1 : 2) + tr("unit.prefix.kilo");
    return format_fixed(std::round(f), 0);
}

std::string time_text(double t, double span) {
    const int decimals = span < 0.01 ? 5 : span < 0.1 ? 4 : span < 1 ? 3 : 2;
    return format_fixed(t, decimals) + " " + tr("unit.s");
}

std::string band_name(std::size_t k, std::size_t levels, GlyphMode g) {
    const std::size_t level = k == 0 ? levels : levels + 1 - k;
    return std::string{k == 0 ? "a" : "d"} + (g == GlyphMode::ascii ? std::to_string(level) : subscript_count(level));
}

std::pair<double, double> band_range(std::size_t k, std::size_t levels, double fs) {
    if (k == 0) return {0.0, fs / std::ldexp(1.0, static_cast<int>(levels) + 1)};
    const auto level = static_cast<int>(levels + 1 - k);
    return {fs / std::ldexp(1.0, level + 1), fs / std::ldexp(1.0, level)};
}

std::string bar(double f, int width, GlyphMode g) {
    if (g == GlyphMode::unicode) return eighths_bar(f, width);
    const auto n = static_cast<int>(std::lround(std::clamp(std::isnan(f) ? 0.0 : f, 0.0, 1.0) * width));
    return std::string(static_cast<std::size_t>(n), '#') + std::string(static_cast<std::size_t>(width - n), ' ');
}

Rgb tint(Rgb c, Rgb toward, double t) {
    auto mix = [t](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(std::lround(a + (b - a) * std::clamp(t, 0.0, 1.0))); };
    return {mix(c.r, toward.r), mix(c.g, toward.g), mix(c.b, toward.b)};
}

ftxui::Element line(const std::vector<Span>& spans, const UiContext& ui) {
    ftxui::Elements out;
    for (const auto& s : spans) {
        auto t = ftxui::text(s.text) | fg(s.color, ui);
        out.push_back(s.bold ? t | ftxui::bold : t);
    }
    return ftxui::hbox(std::move(out));
}

ftxui::Element centered(std::vector<ftxui::Element> rows) {
    ftxui::Elements out{ftxui::filler()};
    for (auto& r : rows) out.push_back(std::move(r) | ftxui::hcenter);
    out.push_back(ftxui::filler());
    return ftxui::vbox(std::move(out));
}

}  // namespace signals::tui
