#include "learn/variant.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace signals::learn {

std::uint64_t Rng::next() {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

long long Rng::range(long long lo, long long hi) {
    if (hi < lo) std::swap(lo, hi);
    const auto span = static_cast<std::uint64_t>(hi - lo) + 1;
    return lo + static_cast<long long>(next() % span);
}

double Rng::uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

double Rng::normal() {
    double s = 0;
    for (int i = 0; i < 12; ++i) s += uniform();
    return s - 6;
}

std::uint64_t seed_of(std::string_view task_id, int variant, int stream) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : fmt::format("{}/{}/{}", task_id, variant, stream)) h = (h ^ c) * 1099511628211ULL;
    return h;
}

ParamValues draw_params(std::span<const ParamSpec> specs, std::string_view task_id, int variant) {
    Rng rng{seed_of(task_id, variant)};
    ParamValues out;
    const auto integer = [&](const ParamSpec& p) { return rng.range(std::llround(p.min), std::llround(p.max)); };
    for (const auto& p : specs) {
        std::string text;
        switch (p.kind) {
        case ParamSpec::Kind::integer: text = std::to_string(integer(p)); break;
        case ParamSpec::Kind::ints:
            for (int i = 0; i < p.count; ++i) text += (i ? " " : "") + std::to_string(integer(p));
            break;
        case ParamSpec::Kind::real: text = fmt::format("{:.{}f}", p.min + rng.uniform() * (p.max - p.min), p.decimals); break;
        case ParamSpec::Kind::choice:
            if (!p.choices.empty()) text = p.choices[static_cast<std::size_t>(rng.range(0, static_cast<long long>(p.choices.size()) - 1))];
            break;
        }
        out[p.name] = std::move(text);
    }
    return out;
}

namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string_view::npos) return {};
    return std::string{s.substr(b, s.find_last_not_of(" \t") - b + 1)};
}

template <class F>
std::string scan(std::string_view text, F on_name) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto open = text.find("{{", i);
        if (open == std::string_view::npos) break;
        const auto close = text.find("}}", open + 2);
        if (close == std::string_view::npos) break;
        out.append(text.substr(i, open - i));
        out += on_name(trim(text.substr(open + 2, close - open - 2)), text.substr(open, close + 2 - open));
        i = close + 2;
    }
    out.append(text.substr(std::min(i, text.size())));
    return out;
}

}  // namespace

std::string substitute(std::string_view text, const ParamValues& values) {
    return scan(text, [&](const std::string& name, std::string_view whole) {
        const auto it = values.find(name);
        return it == values.end() ? std::string{whole} : it->second;
    });
}

std::vector<std::string> placeholders(std::string_view text) {
    std::vector<std::string> out;
    scan(text, [&](const std::string& name, std::string_view whole) {
        if (std::ranges::find(out, name) == out.end()) out.push_back(name);
        return std::string{whole};
    });
    return out;
}

}  // namespace signals::learn
