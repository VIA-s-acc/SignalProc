#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace signals::tui {

class MinMaxPyramid {
public:
    struct Range {
        float lo, hi;
    };

    // Keeps a view of x, not a copy: the samples must outlive the pyramid.
    explicit MinMaxPyramid(std::span<const float> x);

    std::vector<Range> query(std::size_t begin, std::size_t end, std::size_t columns) const;
    std::size_t size() const { return x_.size(); }
    std::size_t bytes() const;

private:
    Range range(std::size_t a, std::size_t b) const;

    std::span<const float> x_;
    std::vector<std::vector<Range>> levels_;
};

}  // namespace signals::tui
