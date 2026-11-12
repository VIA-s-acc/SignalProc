#include "svg/svg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <numeric>
#include <string_view>
#include <utility>

#include <fmt/format.h>

namespace signals::svg {
namespace {

constexpr std::string_view kBackground = "#FFFFFF", kInk = "#222222", kGrid = "#E4E4E4", kFrame = "#888888", kMark = "#999999";
constexpr double kLeft = 72, kRight = 24, kTop = 40, kBottom = 52;
constexpr std::size_t kMaxRows = 300, kMaxCols = 600;

std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string number(double v) {
    if (std::abs(v) < 1e-12) return "0";
    return (v < 0 ? "−" : "") + fmt::format("{:.6g}", std::abs(v));
}

std::string data(double v) { return fmt::format("{}", v); }

double fraction(const Axis& a, double v) {
    if (a.scale == Scale::log10) {
        if (!(v > 0) || !(a.lo > 0) || !(a.hi > a.lo)) return NAN;
        return (std::log10(v) - std::log10(a.lo)) / (std::log10(a.hi) - std::log10(a.lo));
    }
    return a.hi == a.lo ? 0.5 : (v - a.lo) / (a.hi - a.lo);
}

struct Tick {
    double value;
    std::string label;
};

std::vector<Tick> linear_ticks(double lo, double hi, int most) {
    std::vector<Tick> out;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return out;
    const double raw = (hi - lo) / most;
    const double base = std::pow(10.0, std::floor(std::log10(raw)));
    double step = 10 * base;
    for (double f : {1.0, 2.0, 2.5, 5.0})
        if (base * f >= raw) {
            step = base * f;
            break;
        }
    for (double k = std::ceil(lo / step - 1e-9); k * step <= hi + step * 1e-9; ++k) out.push_back({k * step, number(k * step)});
    return out;
}

std::vector<Tick> log_ticks(double lo, double hi) {
    std::vector<Tick> out;
    if (!(lo > 0) || !(hi > lo)) return out;
    const auto first = static_cast<int>(std::floor(std::log10(lo))), last = static_cast<int>(std::ceil(std::log10(hi)));
    const bool fine = last - first <= 2;
    for (int e = first; e <= last; ++e)
        for (double m : fine ? std::vector<double>{1, 2, 5} : std::vector<double>{1}) {
            const double v = m * std::pow(10.0, e);
            if (v >= lo * (1 - 1e-9) && v <= hi * (1 + 1e-9)) out.push_back({v, number(v)});
        }
    return out;
}

std::string pi_label(int num, int den) {
    if (num == 0) return "0";
    const int g = std::gcd(std::abs(num), den);
    num /= g;
    den /= g;
    const int a = std::abs(num);
    return (num < 0 ? "−" : "") + (a == 1 ? std::string{"π"} : std::to_string(a) + "π") + (den == 1 ? "" : "/" + std::to_string(den));
}

std::vector<Tick> pi_ticks(double lo, double hi) {
    std::vector<Tick> out;
    if (!(hi > lo)) return out;
    int den = 1;
    for (int d : {4, 2, 1})
        if ((hi - lo) / (std::numbers::pi / d) <= 8) {
            den = d;
            break;
        }
    const double step = std::numbers::pi / den;
    for (auto k = static_cast<int>(std::ceil(lo / step - 1e-9)); k * step <= hi + 1e-9; ++k) out.push_back({k * step, pi_label(k, den)});
    return out;
}

std::vector<Tick> ticks(const Axis& a, int most) {
    switch (a.scale) {
    case Scale::log10: return log_ticks(a.lo, a.hi);
    case Scale::pi: return pi_ticks(a.lo, a.hi);
    case Scale::linear: break;
    }
    return linear_ticks(std::min(a.lo, a.hi), std::max(a.lo, a.hi), most);
}

std::string dash_attr(Dash d) {
    switch (d) {
    case Dash::dashed: return " stroke-dasharray=\"6 4\"";
    case Dash::dotted: return " stroke-dasharray=\"1.5 3\"";
    case Dash::solid: break;
    }
    return {};
}

std::vector<double> xs_of(const Series& s, const Axis& x, std::size_t n) {
    if (s.x.size() == n) return s.x;
    std::vector<double> out(n);
    for (std::size_t k = 0; k < n; ++k)
        out[k] = n == 1 ? (x.lo + x.hi) / 2 : x.lo + (x.hi - x.lo) * static_cast<double>(k) / static_cast<double>(n - 1);
    return out;
}

struct Frame {
    double left, top, width, height;
};

std::string header(int width, int height, const Frame& f) {
    auto out = fmt::format("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\" "
                           "font-family=\"Helvetica, Arial, sans-serif\" font-size=\"12\">\n",
                           width, height);
    out += fmt::format("<rect width=\"{}\" height=\"{}\" fill=\"{}\"/>\n", width, height, kBackground);
    out += fmt::format("<defs><clipPath id=\"area\"><rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\"/></clipPath></defs>\n", f.left, f.top,
                       f.width, f.height);
    return out;
}

std::string title(std::string_view text, int width) {
    if (text.empty()) return {};
    return fmt::format("<text x=\"{}\" y=\"24\" text-anchor=\"middle\" font-size=\"15\" fill=\"{}\">{}</text>\n", width / 2, kInk, escape(text));
}

std::string axes(const Axis& x, const Axis& y, const Frame& f, bool grid) {
    std::string out;
    for (const auto& t : ticks(x, 8)) {
        const double p = f.left + fraction(x, t.value) * f.width;
        if (!std::isfinite(p) || p < f.left - 0.5 || p > f.left + f.width + 0.5) continue;
        if (grid) out += fmt::format("<line x1=\"{0:.2f}\" y1=\"{1:.2f}\" x2=\"{0:.2f}\" y2=\"{2:.2f}\" stroke=\"{3}\"/>\n", p, f.top, f.top + f.height, kGrid);
        out += fmt::format("<text x=\"{:.2f}\" y=\"{:.2f}\" text-anchor=\"middle\" fill=\"{}\">{}</text>\n", p, f.top + f.height + 16, kInk, escape(t.label));
    }
    for (const auto& t : ticks(y, 6)) {
        const double p = f.top + (1 - fraction(y, t.value)) * f.height;
        if (!std::isfinite(p) || p < f.top - 0.5 || p > f.top + f.height + 0.5) continue;
        if (grid) out += fmt::format("<line x1=\"{0:.2f}\" y1=\"{1:.2f}\" x2=\"{2:.2f}\" y2=\"{1:.2f}\" stroke=\"{3}\"/>\n", f.left, p, f.left + f.width, kGrid);
        out += fmt::format("<text x=\"{:.2f}\" y=\"{:.2f}\" text-anchor=\"end\" fill=\"{}\">{}</text>\n", f.left - 6, p + 4, kInk, escape(t.label));
    }
    if (!x.label.empty())
        out += fmt::format("<text x=\"{:.2f}\" y=\"{:.2f}\" text-anchor=\"middle\" fill=\"{}\">{}</text>\n", f.left + f.width / 2, f.top + f.height + 38, kInk,
                           escape(x.label));
    if (!y.label.empty())
        out += fmt::format("<text transform=\"translate(16 {:.2f}) rotate(-90)\" text-anchor=\"middle\" fill=\"{}\">{}</text>\n", f.top + f.height / 2, kInk,
                           escape(y.label));
    return out;
}

std::string frame_rect(const Frame& f) {
    return fmt::format("<rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\" fill=\"none\" stroke=\"{}\"/>\n", f.left, f.top, f.width, f.height,
                       kFrame);
}

}  // namespace

std::string hex(unsigned r, unsigned g, unsigned b) { return fmt::format("#{:02X}{:02X}{:02X}", r & 255U, g & 255U, b & 255U); }

std::string to_svg(const Chart& c, int width, int height) {
    const Frame f{kLeft, kTop, width - kLeft - kRight, height - kTop - kBottom};
    const auto px = [&](double v) { return f.left + fraction(c.x, v) * f.width; };
    const auto py = [&](double v) { return f.top + (1 - fraction(c.y, v)) * f.height; };
    auto out = header(width, height, f);
    out += title(c.title, width);
    out += axes(c.x, c.y, f, true);
    for (double m : c.x_marks)
        out += fmt::format("<line x1=\"{0:.2f}\" y1=\"{1:.2f}\" x2=\"{0:.2f}\" y2=\"{2:.2f}\" stroke=\"{3}\" stroke-dasharray=\"4 3\"/>\n", px(m), f.top,
                           f.top + f.height, kMark);
    for (double m : c.y_marks)
        out += fmt::format("<line x1=\"{0:.2f}\" y1=\"{1:.2f}\" x2=\"{2:.2f}\" y2=\"{1:.2f}\" stroke=\"{3}\" stroke-dasharray=\"4 3\"/>\n", f.left, py(m),
                           f.left + f.width, kMark);
    out += "<g clip-path=\"url(#area)\" fill=\"none\" stroke-width=\"1.4\" stroke-linejoin=\"round\">\n";
    for (const auto& s : c.series) {
        if (!s.lo.empty() && s.lo.size() == s.hi.size()) {
            const auto xs = xs_of(s, c.x, s.lo.size());
            std::string points;
            for (std::size_t k = 0; k < xs.size(); ++k) points += fmt::format("{:.2f},{:.2f} ", px(xs[k]), py(s.hi[k]));
            for (std::size_t k = xs.size(); k-- > 0;) points += fmt::format("{:.2f},{:.2f} ", px(xs[k]), py(s.lo[k]));
            out += fmt::format("<polygon points=\"{}\" fill=\"{}\" fill-opacity=\"0.35\" stroke=\"{}\" stroke-width=\"0.6\"/>\n", points, s.color, s.color);
        }
        if (s.y.empty()) continue;
        const auto xs = xs_of(s, c.x, s.y.size());
        if (s.stems) {
            const double base = py(std::clamp(0.0, std::min(c.y.lo, c.y.hi), std::max(c.y.lo, c.y.hi)));
            for (std::size_t k = 0; k < xs.size(); ++k) {
                if (!std::isfinite(s.y[k])) continue;
                out += fmt::format("<line x1=\"{0:.2f}\" y1=\"{1:.2f}\" x2=\"{0:.2f}\" y2=\"{2:.2f}\" stroke=\"{3}\"/>\n", px(xs[k]), base, py(s.y[k]), s.color);
                out += fmt::format("<circle cx=\"{:.2f}\" cy=\"{:.2f}\" r=\"2.6\" fill=\"{}\" stroke=\"none\"/>\n", px(xs[k]), py(s.y[k]), s.color);
            }
            continue;
        }
        std::string points;
        std::size_t count = 0;
        const auto flush = [&] {
            if (count >= 2) out += fmt::format("<polyline points=\"{}\" stroke=\"{}\"{}/>\n", points, s.color, dash_attr(s.dash));
            points.clear();
            count = 0;
        };
        for (std::size_t k = 0; k < xs.size(); ++k) {
            const double a = px(xs[k]), b = py(s.y[k]);
            if (!std::isfinite(a) || !std::isfinite(b)) {
                flush();
                continue;
            }
            points += fmt::format("{:.2f},{:.2f} ", a, b);
            ++count;
        }
        flush();
    }
    out += "</g>\n";
    out += frame_rect(f);
    std::vector<const Series*> named;
    for (const auto& s : c.series)
        if (!s.name.empty()) named.push_back(&s);
    if (named.size() > 1) {
        double y = f.top + 14;
        for (const auto* s : named) {
            out += fmt::format("<rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"14\" height=\"3\" fill=\"{}\"/>\n", f.left + f.width - 150, y - 4, s->color);
            out += fmt::format("<text x=\"{:.2f}\" y=\"{:.2f}\" fill=\"{}\">{}</text>\n", f.left + f.width - 130, y, kInk, escape(s->name));
            y += 16;
        }
    }
    out += "</svg>\n";
    return out;
}

std::string to_csv(const Chart& c) {
    std::size_t n = 0;
    bool wide = !c.series.empty();
    const std::vector<double>* shared_x = nullptr;
    for (const auto& s : c.series) {
        const auto size = s.y.empty() ? s.lo.size() : s.y.size();
        if (n == 0) n = size;
        if (size != n || (!s.y.empty() && !s.lo.empty())) wide = false;
        if (!s.x.empty()) {
            if (!shared_x) shared_x = &s.x;
            else if (*shared_x != s.x) wide = false;
        }
    }
    if (shared_x)
        for (const auto& s : c.series)
            if (s.x.empty()) wide = false;
    std::string out;
    if (wide) {
        const auto xs = shared_x ? *shared_x : xs_of(Series{}, c.x, n);
        out += "x";
        for (const auto& s : c.series) out += s.y.empty() ? "," + s.name + " min," + s.name + " max" : "," + s.name;
        out += "\n";
        for (std::size_t k = 0; k < n; ++k) {
            out += data(xs[k]);
            for (const auto& s : c.series) out += s.y.empty() ? "," + data(s.lo[k]) + "," + data(s.hi[k]) : "," + data(s.y[k]);
            out += "\n";
        }
        return out;
    }
    out += "series,x,y\n";
    for (const auto& s : c.series) {
        if (!s.y.empty()) {
            const auto xs = xs_of(s, c.x, s.y.size());
            for (std::size_t k = 0; k < xs.size(); ++k) out += s.name + "," + data(xs[k]) + "," + data(s.y[k]) + "\n";
        }
        if (!s.lo.empty()) {
            const auto xs = xs_of(s, c.x, s.lo.size());
            for (std::size_t k = 0; k < xs.size(); ++k) out += s.name + " min," + data(xs[k]) + "," + data(s.lo[k]) + "\n";
            for (std::size_t k = 0; k < xs.size(); ++k) out += s.name + " max," + data(xs[k]) + "," + data(s.hi[k]) + "\n";
        }
    }
    return out;
}

std::string to_svg(const Heatmap& m, int width, int height) {
    const Frame f{kLeft, kTop, width - kLeft - kRight, height - kTop - kBottom};
    auto out = header(width, height, f);
    out += title(m.title, width);
    const std::size_t rows = std::min(m.rows, kMaxRows), cols = std::min(m.cols, kMaxCols);
    const auto color = m.color ? m.color : [&m](double v) {
        const double t = m.ceil > m.floor ? std::clamp((v - m.floor) / (m.ceil - m.floor), 0.0, 1.0) : 0.0;
        const auto g = static_cast<unsigned>(std::lround(255 * t));
        return hex(g, g, g);
    };
    if (rows > 0 && cols > 0 && m.values.size() >= m.rows * m.cols) {
        out += "<g shape-rendering=\"crispEdges\">\n";
        const double cw = f.width / static_cast<double>(cols), ch = f.height / static_cast<double>(rows);
        for (std::size_t r = 0; r < rows; ++r) {
            const auto r0 = r * m.rows / rows, r1 = std::max(r0 + 1, (r + 1) * m.rows / rows);
            for (std::size_t c = 0; c < cols; ++c) {
                const auto c0 = c * m.cols / cols, c1 = std::max(c0 + 1, (c + 1) * m.cols / cols);
                double sum = 0;
                std::size_t count = 0;
                for (auto i = r0; i < r1; ++i)
                    for (auto j = c0; j < c1; ++j)
                        if (const double v = m.values[i * m.cols + j]; std::isfinite(v)) {
                            sum += v;
                            ++count;
                        }
                const double v = count ? sum / static_cast<double>(count) : NAN;
                out += fmt::format("<rect class=\"cell\" x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\" fill=\"{}\"/>\n", f.left + cw * static_cast<double>(c),
                                   f.top + ch * static_cast<double>(r), cw + 0.3, ch + 0.3, std::isfinite(v) ? color(v) : std::string{kBackground});
            }
        }
        out += "</g>\n";
    }
    out += axes(m.x, m.y, f, false);
    out += frame_rect(f);
    out += "</svg>\n";
    return out;
}

std::string to_csv(const Heatmap& m) {
    std::string out = "row";
    for (std::size_t c = 0; c < m.cols; ++c) out += "," + std::to_string(c);
    out += "\n";
    for (std::size_t r = 0; r < m.rows; ++r) {
        out += std::to_string(r);
        for (std::size_t c = 0; c < m.cols; ++c) out += "," + (r * m.cols + c < m.values.size() ? data(m.values[r * m.cols + c]) : std::string{});
        out += "\n";
    }
    return out;
}

}  // namespace signals::svg
