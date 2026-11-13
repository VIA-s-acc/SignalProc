#include "tui/render/svg_frame.hpp"

#include <charconv>
#include <map>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <ftxui/screen/color_info.hpp>

namespace signals::tui {

namespace {

constexpr std::string_view kFonts = R"(ui-monospace, SFMono-Regular, Menlo, Consolas, "DejaVu Sans Mono", "Liberation Mono", monospace)";

std::string num(double v) {
    auto s = fmt::format("{:.2f}", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

std::string hex(Rgb c) { return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b); }

void escape(std::string& out, std::string_view text) {
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
}

int parse_int(std::string_view s) {
    int v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

Rgb from_info(const ftxui::ColorInfo& info) { return {info.red, info.green, info.blue}; }

struct Look {
    Rgb fg, bg;
    bool bold = false, dim = false, italic = false, underlined = false;
    bool same_text(const Look& o) const { return fg == o.fg && bold == o.bold && dim == o.dim && italic == o.italic && underlined == o.underlined; }
};

Look look_of(const ftxui::Cell& c, const SvgStyle& st) {
    Look l{to_rgb(c.foreground_color).value_or(st.fg), to_rgb(c.background_color).value_or(st.bg), c.bold, c.dim, c.italic,
           c.underlined || c.underlined_double};
    if (c.inverted) std::swap(l.fg, l.bg);
    return l;
}

bool blank(const std::string& ch) { return ch.empty() || ch == " "; }

// SVG positions glyphs in an x list per UTF-16 code unit, so a cell outside the BMP or with
// combining marks would shift every later glyph of its run; such cells get their own element.
bool single_bmp(const std::string& ch) {
    const auto lead = static_cast<unsigned char>(ch[0]);
    const std::size_t len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    return len <= 3 && ch.size() == len;
}

std::string row_markup(const ftxui::Screen& s, int y, const SvgStyle& st) {
    std::string out;
    const double top = st.padding + y * st.cell_h;
    for (int x = 0; x < s.dimx();) {
        const auto bg = look_of(s.CellAt(x, y), st).bg;
        int end = x + 1;
        while (end < s.dimx() && look_of(s.CellAt(end, y), st).bg == bg) ++end;
        if (!(bg == st.bg))
            out += fmt::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="{}"/>)", num(st.padding + x * st.cell_w), num(top), num((end - x) * st.cell_w),
                               num(st.cell_h), hex(bg));
        x = end;
    }
    const auto base = num(top + st.cell_h * 0.75);
    std::optional<Look> run;
    std::string xs, glyphs;
    const auto flush = [&] {
        if (!run || glyphs.empty()) return;
        out += fmt::format(R"(<text x="{}" y="{}" fill="{}")", xs, base, hex(run->fg));
        if (run->bold) out += R"( font-weight="bold")";
        if (run->dim) out += R"( opacity="0.6")";
        if (run->italic) out += R"( font-style="italic")";
        if (run->underlined) out += R"( text-decoration="underline")";
        out += ">" + glyphs + "</text>";
        xs.clear();
        glyphs.clear();
        run.reset();
    };
    for (int x = 0; x < s.dimx(); ++x) {
        const auto& cell = s.CellAt(x, y);
        if (blank(cell.character)) continue;
        const auto look = look_of(cell, st);
        const bool alone = !single_bmp(cell.character);
        if (run && (alone || !run->same_text(look))) flush();
        run = look;
        xs += (xs.empty() ? "" : " ") + num(st.padding + x * st.cell_w);
        escape(glyphs, cell.character);
        if (alone) flush();
    }
    flush();
    return out;
}

std::string open_svg(const ftxui::Screen& s, const SvgStyle& st) {
    const auto w = num(2 * st.padding + s.dimx() * st.cell_w), h = num(2 * st.padding + s.dimy() * st.cell_h);
    return fmt::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{0}" height="{1}" viewBox="0 0 {0} {1}" font-family='{2}' font-size="{3}">)"
                       R"(<rect width="{0}" height="{1}" rx="{4}" fill="{5}"/>)",
                       w, h, kFonts, num(st.font_size), num(st.radius), hex(st.bg));
}

}  // namespace

std::optional<Rgb> to_rgb(const ftxui::Color& c) {
    const auto code = c.Print(false);
    if (code.starts_with("38;2;")) {
        const auto a = code.find(';', 5), b = code.find(';', a + 1);
        return Rgb{static_cast<std::uint8_t>(parse_int(std::string_view{code}.substr(5, a - 5))),
                   static_cast<std::uint8_t>(parse_int(std::string_view{code}.substr(a + 1, b - a - 1))),
                   static_cast<std::uint8_t>(parse_int(std::string_view{code}.substr(b + 1)))};
    }
    if (code.starts_with("38;5;")) return from_info(ftxui::GetColorInfo(static_cast<ftxui::Color::Palette256>(parse_int(std::string_view{code}.substr(5)))));
    const int n = parse_int(code);
    if (n >= 30 && n <= 37) return from_info(ftxui::GetColorInfo(static_cast<ftxui::Color::Palette16>(n - 30)));
    if (n >= 90 && n <= 97) return from_info(ftxui::GetColorInfo(static_cast<ftxui::Color::Palette16>(n - 90 + 8)));
    return std::nullopt;
}

std::string svg_still(const ftxui::Screen& screen, const SvgStyle& style) {
    std::string out = open_svg(screen, style);
    for (int y = 0; y < screen.dimy(); ++y) out += row_markup(screen, y, style);
    return out + "</svg>\n";
}

std::string svg_animation(std::span<const SvgFrame> frames, const SvgStyle& style) {
    if (frames.empty()) return {};
    double total = 0;
    for (const auto& f : frames) total += f.seconds;
    std::map<std::string, std::size_t> ids;
    std::string defs, body, css;
    double start = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& screen = frames[i].screen;
        body += fmt::format(R"(<g class="f{}">)", i);
        for (int y = 0; y < screen.dimy(); ++y) {
            auto markup = row_markup(screen, y, style);
            if (markup.empty()) continue;
            auto [it, added] = ids.emplace(std::move(markup), ids.size());
            if (added) defs += fmt::format(R"(<g id="r{}">{}</g>)", it->second, it->first);
            body += fmt::format(R"(<use href="#r{}"/>)", it->second);
        }
        body += "</g>";
        const auto from = num(100 * start / total), to = num(100 * (start + frames[i].seconds) / total);
        css += fmt::format(".f{0}{{{1}animation:a{0} {2}s step-end infinite}}", i, i == 0 ? "" : "opacity:0;", num(total));
        css += i == 0 ? fmt::format("@keyframes a{}{{0%{{opacity:1}}{}%{{opacity:0}}}}", i, to)
                      : fmt::format("@keyframes a{}{{0%{{opacity:0}}{}%{{opacity:1}}{}%{{opacity:0}}}}", i, from, to);
        start += frames[i].seconds;
    }
    return open_svg(frames.front().screen, style) + "<style>" + css + "</style><defs>" + defs + "</defs>" + body + "</svg>\n";
}

}  // namespace signals::tui
