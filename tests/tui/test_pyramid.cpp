#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

#include "tui/data/pyramid.hpp"

using signals::tui::MinMaxPyramid;

TEST_CASE("pyramid queries equal brute force min and max", "[tui][pyramid]") {
    std::mt19937 rng{11};
    std::uniform_real_distribution<float> u{-1.0f, 1.0f};
    std::vector<float> x(100'003);
    for (auto& v : x) v = u(rng);
    const MinMaxPyramid p{x};
    std::uniform_int_distribution<std::size_t> pos{0, x.size()};
    std::uniform_int_distribution<std::size_t> cols{1, 400};
    for (int q = 0; q < 200; ++q) {
        std::size_t a = pos(rng), b = pos(rng);
        if (a > b) std::swap(a, b);
        if (a == b) continue;
        const std::size_t c = cols(rng), len = b - a;
        const auto r = p.query(a, b, c);
        REQUIRE(r.size() == c);
        for (std::size_t k = 0; k < c; ++k) {
            const std::size_t s = a + k * len / c, e = a + (k + 1) * len / c;
            INFO("query " << q << " column " << k);
            if (e > s) {
                const auto [lo, hi] = std::minmax_element(x.begin() + static_cast<std::ptrdiff_t>(s), x.begin() + static_cast<std::ptrdiff_t>(e));
                CHECK(r[k].lo == *lo);
                CHECK(r[k].hi == *hi);
            } else {
                CHECK(r[k].lo == r[k].hi);
            }
        }
    }
}

TEST_CASE("pyramid memory is about a quarter of the signal", "[tui][pyramid]") {
    std::vector<float> x(1 << 20, 0.5f);
    const MinMaxPyramid p{x};
    CHECK(p.size() == x.size());
    CHECK(p.bytes() <= x.size() * sizeof(float) / 4 + 4096);
}

TEST_CASE("zoomed past the samples each column takes the nearest sample", "[tui][pyramid]") {
    std::vector<float> x{1, 2, 3};
    const MinMaxPyramid p{x};
    auto q = p.query(0, 3, 6);
    REQUIRE(q.size() == 6);
    CHECK(q[0].lo == 1);
    CHECK(q[0].hi == 1);
    CHECK(q[5].hi == 3);
    CHECK(p.query(2, 2, 10).empty());
    CHECK(p.query(0, 3, 0).empty());
    CHECK(p.query(1, 100, 2).size() == 2);
}
