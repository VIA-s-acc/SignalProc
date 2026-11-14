#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/random.hpp"

using namespace signals::core;

TEST_CASE("portable_log agrees with std::log to a few ulps", "[random]") {
    std::mt19937_64 rng{42};
    for (int k = 0; k < 200000; ++k) {
        const double x = std::ldexp(0.5 + 0.5 * uniform53(rng), static_cast<int>(rng() % 2000) - 1000);
        const double want = std::log(x), got = portable_log(x);
        INFO(x);
        CHECK(std::abs(got - want) <= 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(want)));
    }
    for (const double x : {1.0, 0.5, 2.0, 0.999999999, 1.000000001, std::numeric_limits<double>::min(), 1e-300})
        CHECK(std::abs(portable_log(x) - std::log(x)) <= 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(std::log(x))));
    CHECK(portable_log(1.0) == 0.0);
}

TEST_CASE("uniform53 covers [0, 1) with 53 random bits", "[random]") {
    std::mt19937_64 rng{7};
    double lo = 1, hi = 0, sum = 0;
    for (int k = 0; k < 100000; ++k) {
        const double u = uniform53(rng);
        REQUIRE(u >= 0.0);
        REQUIRE(u < 1.0);
        lo = std::min(lo, u), hi = std::max(hi, u), sum += u;
    }
    CHECK(lo < 1e-3);
    CHECK(hi > 1 - 1e-3);
    CHECK(std::abs(sum / 100000 - 0.5) < 0.01);
}

TEST_CASE("normal draws have mean 0 and variance 1", "[random]") {
    NormalDraws normal{11};
    const int n = 400000;
    double sum = 0, squares = 0, fourth = 0;
    for (int k = 0; k < n; ++k) {
        const double x = normal.next();
        sum += x, squares += x * x, fourth += x * x * x * x;
    }
    const double mean = sum / n, var = squares / n - mean * mean;
    CHECK(std::abs(mean) < 0.01);
    CHECK(std::abs(var - 1) < 0.01);
    CHECK(std::abs(fourth / n - 3) < 0.05);
}

TEST_CASE("tpdf draws are triangular on (-1, 1)", "[random]") {
    TpdfDraws tpdf{3};
    double sum = 0, squares = 0;
    for (int k = 0; k < 200000; ++k) {
        const double d = tpdf.next();
        REQUIRE(d > -1.0);
        REQUIRE(d < 1.0);
        sum += d, squares += d * d;
    }
    CHECK(std::abs(sum / 200000) < 0.01);
    CHECK(std::abs(squares / 200000 - 1.0 / 6) < 0.01);
}

TEST_CASE("seeded draws are the same on every platform", "[random]") {
    NormalDraws normal{1};
    std::vector<double> got;
    for (int k = 0; k < 6; ++k) got.push_back(normal.next());
    const std::vector<double> want{-0x1.42c3b2b72217p-5, -0x1.8c1da014dda08p-2, -0x1.fdd85e535a47ap-3, 0x1.5fa75918ca312p-1, -0x1.bfaac17196979p-5, -0x1.971d689089fdcp-1};
    CHECK(got == want);
    TpdfDraws tpdf{1};
    std::vector<double> dither;
    for (int k = 0; k < 4; ++k) dither.push_back(tpdf.next());
    const std::vector<double> want_dither{-0x1.4ba9e0cf612p-9, 0x1.b883e78806c2p-2, -0x1.1ef49abf992e1p-1, 0x1.95d6c4f1d5aa2p-2};
    CHECK(dither == want_dither);
}
