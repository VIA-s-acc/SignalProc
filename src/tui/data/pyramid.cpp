#include "tui/data/pyramid.hpp"

#include <algorithm>

namespace signals::tui {
namespace {

constexpr std::size_t kBase = 16;

MinMaxPyramid::Range merge(MinMaxPyramid::Range a, MinMaxPyramid::Range b) {
    return {std::min(a.lo, b.lo), std::max(a.hi, b.hi)};
}

}  // namespace

MinMaxPyramid::MinMaxPyramid(std::span<const float> x) : x_{x} {
    if (x.size() < kBase) return;
    std::vector<Range> level((x.size() + kBase - 1) / kBase);
    for (std::size_t i = 0; i < level.size(); ++i) {
        const auto first = x.begin() + static_cast<std::ptrdiff_t>(i * kBase);
        const auto last = x.begin() + static_cast<std::ptrdiff_t>(std::min(x.size(), (i + 1) * kBase));
        const auto [lo, hi] = std::minmax_element(first, last);
        level[i] = {*lo, *hi};
    }
    levels_.push_back(std::move(level));
    while (levels_.back().size() > 1) {
        const auto& prev = levels_.back();
        std::vector<Range> next((prev.size() + 1) / 2);
        for (std::size_t i = 0; i < next.size(); ++i)
            next[i] = 2 * i + 1 < prev.size() ? merge(prev[2 * i], prev[2 * i + 1]) : prev[2 * i];
        levels_.push_back(std::move(next));
    }
}

MinMaxPyramid::Range MinMaxPyramid::range(std::size_t a, std::size_t b) const {
    Range r{x_[a], x_[a]};
    while (a < b) {
        int best = -1;
        std::size_t bin = kBase;
        for (std::size_t k = 0; k < levels_.size(); ++k, bin *= 2) {
            if (a % bin != 0 || a + bin > b) break;
            best = static_cast<int>(k);
        }
        if (best < 0) {
            r = merge(r, {x_[a], x_[a]});
            ++a;
        } else {
            const std::size_t width = kBase << best;
            r = merge(r, levels_[static_cast<std::size_t>(best)][a / width]);
            a += width;
        }
    }
    return r;
}

std::vector<MinMaxPyramid::Range> MinMaxPyramid::query(std::size_t begin, std::size_t end, std::size_t columns) const {
    end = std::min(end, x_.size());
    if (begin >= end || columns == 0) return {};
    const std::size_t len = end - begin;
    std::vector<Range> out(columns);
    for (std::size_t c = 0; c < columns; ++c) {
        const std::size_t a = begin + c * len / columns, b = begin + (c + 1) * len / columns;
        if (b > a) {
            out[c] = range(a, b);
        } else {
            const std::size_t i = std::min(end - 1, begin + static_cast<std::size_t>((static_cast<double>(c) + 0.5) * static_cast<double>(len) / static_cast<double>(columns)));
            out[c] = {x_[i], x_[i]};
        }
    }
    return out;
}

std::size_t MinMaxPyramid::bytes() const {
    std::size_t n = 0;
    for (const auto& l : levels_) n += l.size() * sizeof(Range);
    return n;
}

}  // namespace signals::tui
