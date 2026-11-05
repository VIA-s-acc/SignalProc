#include "tui/widgets/format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>

namespace signals::tui {
namespace {

constexpr std::string_view kMinus = "−";

std::string group_thousands(std::string digits) {
    std::string out;
    const auto n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += ' ';
        out += digits[i];
    }
    return out;
}

std::string fixed(double v, int decimals, bool trim) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", std::max(0, decimals), std::abs(v));
    std::string s{buf};
    auto dot = s.find('.');
    std::string int_part = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (trim)
        while (!frac.empty() && frac.back() == '0') frac.pop_back();
    std::string out = group_thousands(int_part);
    if (!frac.empty()) out += "," + frac;
    const bool zero = std::all_of(s.begin(), s.end(), [](char c) { return c == '0' || c == '.'; });
    return (v < 0 && !zero ? std::string{kMinus} : std::string{}) + out;
}

std::string superscript(int e) {
    static constexpr std::array<std::string_view, 10> digits{"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string out = e < 0 ? "⁻" : "";
    for (char c : std::to_string(std::abs(e))) out += digits[static_cast<std::size_t>(c - '0')];
    return out;
}

double nice(double x, bool round) {
    const double e = std::floor(std::log10(x));
    const double f = x / std::pow(10.0, e);
    double nf;
    if (round) nf = f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10;
    else nf = f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10;
    return nf * std::pow(10.0, e);
}

std::string log_label(double v) {
    if (v >= 1000) return fixed(v / 1000, v / 1000 < 10 && std::fmod(v / 1000, 1) != 0 ? 1 : 0, true) + " к";
    return fixed(v, v < 1 ? 2 : 0, true);
}

std::string pi_label(int num, int den) {
    if (num == 0) return "0";
    const int g = std::gcd(std::abs(num), den);
    num /= g;
    den /= g;
    const std::string sign = num < 0 ? std::string{kMinus} : "";
    const int a = std::abs(num);
    const std::string head = a == 1 ? "π" : std::to_string(a) + "π";
    return sign + head + (den == 1 ? "" : "/" + std::to_string(den));
}

}  // namespace

std::string format_number(double v, int significant) {
    if (std::isnan(v)) return "—";
    if (std::isinf(v)) return v > 0 ? "∞" : std::string{kMinus} + "∞";
    if (v == 0) return "0";
    const double a = std::abs(v);
    if (a < 1e-3 || a >= 1e6) {
        int e = static_cast<int>(std::floor(std::log10(a)));
        double m = v / std::pow(10.0, e);
        const double scale = std::pow(10.0, significant - 1);
        m = std::round(m * scale) / scale;
        if (std::abs(m) >= 10) {
            m /= 10;
            ++e;
        }
        return fixed(m, significant - 1, true) + "·10" + superscript(e);
    }
    const int decimals = std::max(0, significant - 1 - static_cast<int>(std::floor(std::log10(a))));
    return fixed(v, decimals, true);
}

std::string format_si(double v, std::string_view unit, int significant) {
    const bool bytes = unit == "Б";
    if (v == 0 || !std::isfinite(v)) return format_number(v, significant) + " " + std::string{unit};
    static constexpr std::array<std::string_view, 6> decimal{"мк", "м", "", "к", "М", "Г"};
    static constexpr std::array<std::string_view, 4> binary{"", "К", "М", "Г"};
    const double a = std::abs(v);
    if (bytes) {
        int k = 0;
        double m = v;
        while (k + 1 < static_cast<int>(binary.size()) && std::abs(m) >= 1024) {
            m /= 1024;
            ++k;
        }
        return format_number(m, significant) + " " + std::string{binary[static_cast<std::size_t>(k)]} + std::string{unit};
    }
    int k = static_cast<int>(std::floor(std::log10(a) / 3));
    k = std::clamp(k, -2, 3);
    const double m = v / std::pow(1000.0, k);
    return format_number(m, significant) + " " + std::string{decimal[static_cast<std::size_t>(k + 2)]} + std::string{unit};
}

std::vector<Tick> nice_ticks(double lo, double hi, int max_ticks) {
    if (!(hi > lo) || max_ticks < 2) return {{lo, format_number(lo)}};
    const double range = nice(hi - lo, false);
    const double d = nice(range / (max_ticks - 1), true);
    const int decimals = std::max(0, -static_cast<int>(std::floor(std::log10(d))));
    std::vector<Tick> out;
    const double eps = d * 1e-9;
    for (double v = std::ceil(lo / d - 1e-9) * d; v <= hi + eps; v += d) {
        const double r = std::abs(v) < eps ? 0.0 : v;
        out.push_back({r, fixed(r, decimals, false)});
    }
    return out;
}

std::vector<Tick> log_ticks(double lo, double hi, int max_ticks) {
    std::vector<Tick> all;
    if (!(hi > lo) || lo <= 0) return all;
    for (int e = static_cast<int>(std::floor(std::log10(lo))); e <= static_cast<int>(std::ceil(std::log10(hi))); ++e)
        for (double m : {1.0, 2.0, 5.0}) {
            const double v = m * std::pow(10.0, e);
            if (v >= lo * (1 - 1e-12) && v <= hi * (1 + 1e-12)) all.push_back({v, log_label(v)});
        }
    if (static_cast<int>(all.size()) <= max_ticks) return all;
    std::vector<Tick> decades;
    for (const auto& t : all)
        if (std::abs(std::log10(t.value) - std::round(std::log10(t.value))) < 1e-9) decades.push_back(t);
    std::size_t step = 1;
    while (max_ticks > 0 && (decades.size() + step - 1) / step > static_cast<std::size_t>(max_ticks)) ++step;
    std::vector<Tick> out;
    for (std::size_t i = 0; i < decades.size(); i += step) out.push_back(decades[i]);
    return out;
}

std::vector<Tick> pi_ticks(double lo, double hi, int max_ticks) {
    std::vector<Tick> out;
    if (!(hi > lo)) return out;
    constexpr double pi = std::numbers::pi;
    for (int den : {4, 2, 1}) {
        for (int mult : {1, 2, 4, 8, 16}) {
            if (den != 1 && mult != 1) continue;
            const double step = pi * mult / den;
            const auto first = static_cast<long>(std::ceil(lo / step - 1e-9)), last = static_cast<long>(std::floor(hi / step + 1e-9));
            if (last - first + 1 > max_ticks) continue;
            for (long k = first; k <= last; ++k) out.push_back({k * step, pi_label(static_cast<int>(k * mult), den)});
            return out;
        }
    }
    return out;
}

std::string eighths_bar(double fraction, int width) {
    static constexpr std::array<std::string_view, 8> parts{"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    if (width <= 0) return {};
    const double f = std::isnan(fraction) ? 0.0 : std::clamp(fraction, 0.0, 1.0);
    const long total = std::lround(f * width * 8);
    const long full = std::min<long>(width, total / 8);
    const long rem = full == width ? 0 : total % 8;
    std::string out;
    for (long i = 0; i < full; ++i) out += "█";
    out += parts[static_cast<std::size_t>(rem)];
    for (long i = full + (rem ? 1 : 0); i < width; ++i) out += ' ';
    return out;
}

}  // namespace signals::tui
