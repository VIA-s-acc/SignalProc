#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "core/metrics.hpp"
#include "core/signal_ops.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("mu-law matches the G.711 companding curve", "[ops][golden]") {
    const auto g = signals::test::load_golden("ops.json");
    const auto x = g["x"].get<std::vector<double>>();
    const auto expected = g["mulaw"].get<std::vector<double>>();
    const auto y = mulaw_encode(x);
    REQUIRE(y.size() == expected.size());
    CHECK(max_abs_error(y, expected) < 1e-15);
}

TEST_CASE("mu-law round trip is exact inside ±1 and clips outside", "[ops]") {
    std::vector<double> x;
    for (int k = -100; k <= 100; ++k) x.push_back(k / 100.0);
    CHECK(max_abs_error(mulaw_decode(mulaw_encode(x)), x) < 1e-12);
    const std::vector<double> wild{2.0, -3.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
    const auto y = mulaw_encode(wild);
    CHECK(y[0] == 1.0);
    CHECK(y[1] == -1.0);
    CHECK(y[2] == 0.0);
    CHECK(y[3] == 1.0);
    for (double v : mulaw_decode(y)) CHECK(std::isfinite(v));
}

TEST_CASE("quantize without dither matches numpy rounding", "[ops][golden]") {
    const auto g = signals::test::load_golden("ops.json");
    const auto y = g["y"].get<std::vector<double>>();
    for (const auto& c : g["quantize"]) {
        const int bits = c["bits"];
        INFO("bits " << bits);
        const auto q = quantize(y, bits, false, 0);
        REQUIRE(q);
        CHECK(max_abs_error(*q, c["y"].get<std::vector<double>>()) == 0);
    }
    CHECK_FALSE(quantize(y, 0, false, 0));
    CHECK_FALSE(quantize(y, 33, false, 0));
}

TEST_CASE("dithered quantization is reproducible and stays within one and a half steps", "[ops]") {
    std::vector<double> x(4096);
    for (std::size_t n = 0; n < x.size(); ++n) x[n] = 0.6 * std::sin(0.01 * static_cast<double>(n));
    const auto a = quantize(x, 8, true, 7);
    const auto b = quantize(x, 8, true, 7);
    const auto c = quantize(x, 8, true, 8);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    CHECK(*a == *b);
    CHECK(*a != *c);
    const double step = std::ldexp(1.0, -7);
    for (std::size_t n = 0; n < x.size(); ++n) {
        CHECK(std::abs((*a)[n] - x[n]) <= 1.5 * step + 1e-15);
        CHECK(std::abs(std::remainder((*a)[n], step)) < 1e-15);
    }
}
