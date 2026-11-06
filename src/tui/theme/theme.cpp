#include "tui/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>
#include <optional>
#include <unordered_map>

#include <toml++/toml.hpp>

extern const char kSignalsThemeDark[];
extern const char kSignalsThemeLight[];

namespace signals::tui {
namespace {

using core::Errc;
using core::make_error;

constexpr std::array<Rgb, 256> kViridis{{
#include "tui/theme/viridis.inc"
}};

constexpr std::array<Rgb, 16> kAnsi16{{{0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
                                       {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
                                       {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
                                       {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}}};

struct Lab {
    double l, a, b;
};

double linearize(std::uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

Lab oklab(Rgb c) {
    const double r = linearize(c.r), g = linearize(c.g), b = linearize(c.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

double distance(Lab x, Lab y) { return (x.l - y.l) * (x.l - y.l) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b); }

Rgb palette256_rgb(int index) {
    static constexpr std::array<std::uint8_t, 6> levels{0, 95, 135, 175, 215, 255};
    if (index >= 232) {
        const auto v = static_cast<std::uint8_t>(8 + 10 * (index - 232));
        return {v, v, v};
    }
    const int i = index - 16;
    return {levels[static_cast<std::size_t>(i / 36)], levels[static_cast<std::size_t>(i / 6 % 6)], levels[static_cast<std::size_t>(i % 6)]};
}

std::optional<Rgb> parse_hex(std::string_view s) {
    if (s.size() != 7 || s[0] != '#') return std::nullopt;
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::array<int, 6> d{};
    for (std::size_t i = 0; i < 6; ++i)
        if ((d[i] = nibble(s[i + 1])) < 0) return std::nullopt;
    return Rgb{static_cast<std::uint8_t>(d[0] * 16 + d[1]), static_cast<std::uint8_t>(d[2] * 16 + d[3]),
               static_cast<std::uint8_t>(d[4] * 16 + d[5])};
}

}  // namespace

core::Result<Theme> parse_theme(std::string_view text) {
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error& e) {
        return make_error(Errc::invalid_argument, std::string{"theme is not valid TOML: "} + std::string{e.description()});
    }
    Theme theme;
    std::map<std::string, Rgb*> colours{{"bg", &theme.bg}, {"fg", &theme.fg}, {"dim", &theme.dim}, {"accent", &theme.accent},
                                        {"ok", &theme.ok}, {"warn", &theme.warn}, {"err", &theme.err}};
    for (std::size_t i = 0; i < theme.trace.size(); ++i) colours["trace" + std::to_string(i + 1)] = &theme.trace[i];
    std::map<std::string, bool> seen;
    for (const auto& [key, node] : table) {
        const std::string k{key.str()};
        if (k == "name") {
            theme.name = node.value_or<std::string>("");
            continue;
        }
        auto it = colours.find(k);
        if (it == colours.end()) return make_error(Errc::invalid_argument, "unknown theme token: " + k);
        const auto rgb = parse_hex(node.value_or<std::string>(""));
        if (!rgb) return make_error(Errc::invalid_argument, "theme token " + k + " must be #RRGGBB");
        *it->second = *rgb;
        seen[k] = true;
    }
    std::string missing;
    for (const auto& [k, ptr] : colours)
        if (!seen[k]) missing += (missing.empty() ? "" : ", ") + k;
    if (!missing.empty()) return make_error(Errc::invalid_argument, "theme tokens missing: " + missing);
    return theme;
}

Theme builtin_theme(std::string_view name) {
    auto t = parse_theme(name == "light" ? kSignalsThemeLight : kSignalsThemeDark);
    return t ? *t : Theme{};
}

namespace {

int nearest256(Rgb c) {
    static const auto table = [] {
        std::array<Lab, 240> t{};
        for (int i = 16; i < 256; ++i) t[static_cast<std::size_t>(i - 16)] = oklab(palette256_rgb(i));
        return t;
    }();
    thread_local std::unordered_map<std::uint32_t, int> cache;
    const auto key = static_cast<std::uint32_t>(c.r) << 16 | static_cast<std::uint32_t>(c.g) << 8 | c.b;
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    const Lab target = oklab(c);
    int best = 16;
    double best_d = 1e9;
    for (int i = 16; i < 256; ++i) {
        const double d = distance(table[static_cast<std::size_t>(i - 16)], target);
        if (d < best_d) best_d = d, best = i;
    }
    cache.emplace(key, best);
    return best;
}

}  // namespace

ftxui::Color to_color(Rgb c, ColorDepth depth) {
    switch (depth) {
    case ColorDepth::truecolor: return ftxui::Color::RGB(c.r, c.g, c.b);
    case ColorDepth::palette256: return ftxui::Color(static_cast<ftxui::Color::Palette256>(nearest256(c)));
    case ColorDepth::ansi16: {
        const Lab target = oklab(c);
        int best = 0;
        double best_d = 1e9;
        for (int i = 0; i < 16; ++i) {
            const double d = distance(oklab(kAnsi16[static_cast<std::size_t>(i)]), target);
            if (d < best_d) best_d = d, best = i;
        }
        return ftxui::Color(static_cast<ftxui::Color::Palette16>(best));
    }
    case ColorDepth::none: return ftxui::Color(ftxui::Color::Default);
    }
    return ftxui::Color(ftxui::Color::Default);
}

Rgb viridis(double t) {
    const double x = std::isnan(t) ? 0.0 : std::clamp(t, 0.0, 1.0);
    return kViridis[static_cast<std::size_t>(std::lround(x * 255))];
}

}  // namespace signals::tui
