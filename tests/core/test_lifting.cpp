#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "core/design/families.hpp"
#include "core/lifting.hpp"
#include "core/metrics.hpp"
#include "support/banks.hpp"

using namespace signals::core;
using signals::test::signal_of_length;

TEST_CASE("real 5/3 lifting is the LeGall convolution inside and reconstructs any length", "[lifting]") {
    const auto x = signal_of_length(40);
    const auto l = lift53(x);
    REQUIRE(l.s.size() == 20);
    REQUIRE(l.d.size() == 20);
    for (std::size_t n = 2; n + 2 < l.s.size(); ++n) {
        const auto i = 2 * n;
        const double lo = (-x[i - 2] + 2 * x[i - 1] + 6 * x[i] + 2 * x[i + 1] - x[i + 2]) / 8;
        const double hi = x[i + 1] - 0.5 * (x[i] + x[i + 2]);
        CHECK(std::abs(l.s[n] - lo) <= 1e-12);
        CHECK(std::abs(l.d[n] - hi) <= 1e-12);
    }
    for (std::size_t n = 1; n <= 17; ++n) {
        INFO("n=" << n);
        const auto y = signal_of_length(n);
        CHECK(max_abs_error(unlift53(lift53(y)), y) <= 1e-12);
        CHECK(max_abs_error(unlift97(lift97(y)), y) <= 1e-12);
    }
}

TEST_CASE("integer 5/3 lifting is lossless on int16 and int32 extremes", "[lifting]") {
    std::mt19937 rng{11};
    for (std::size_t n : {1, 2, 3, 7, 64, 333}) {
        INFO("n=" << n);
        std::vector<std::int64_t> x(n);
        std::uniform_int_distribution<std::int64_t> pick(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        for (auto& v : x) v = pick(rng);
        if (n > 2) {
            x[0] = std::numeric_limits<std::int32_t>::min();
            x[1] = std::numeric_limits<std::int32_t>::max();
        }
        CHECK(unlift53_int(lift53_int(x)) == x);
    }
    std::vector<std::int32_t> pcm(4096);
    std::uniform_int_distribution<int> s16(-32768, 32767);
    for (auto& v : pcm) v = s16(rng);
    const auto p = lift53_int_levels(pcm, 10);
    REQUIRE(p);
    CHECK(p->bands.size() == 11);
    const auto back = unlift53_int_levels(*p);
    CHECK(std::equal(back.begin(), back.end(), pcm.begin(), pcm.end()));
    CHECK_FALSE(lift53_int_levels(pcm, 31));
    CHECK_FALSE(lift53_int_levels(std::vector<std::int32_t>(10, 1), 5));
}

TEST_CASE("the 9/7 lifting steps reproduce bior4.4", "[lifting]") {
    const auto& c = lift97_constants();
    CHECK(std::abs(c.alpha - -1.586134342) < 1e-8);
    CHECK(std::abs(c.zeta - 1.149604398) < 1e-8);
    const auto bank = wavelet("bior4.4");
    REQUIRE(bank);
    const auto& h0 = bank->h0.coeffs;
    const auto& h1 = bank->h1.coeffs;
    const auto centre0 = static_cast<std::size_t>(std::max_element(h0.begin(), h0.end()) - h0.begin());
    std::vector<double> x(64, 0.0);
    double worst_lo = 0, worst_hi_plus = 0, worst_hi_minus = 0;
    for (int k = -4; k <= 4; ++k) {
        std::fill(x.begin(), x.end(), 0.0);
        x[static_cast<std::size_t>(32 + k)] = 1;
        const auto l = lift97(x);
        worst_lo = std::max(worst_lo, std::abs(l.s[16] - h0[static_cast<std::size_t>(static_cast<int>(centre0) + k)]));
    }
    const auto centre1 = static_cast<std::size_t>(std::max_element(h1.begin(), h1.end(), [](double a, double b) { return std::abs(a) < std::abs(b); }) - h1.begin());
    for (int k = -3; k <= 3; ++k) {
        std::fill(x.begin(), x.end(), 0.0);
        x[static_cast<std::size_t>(33 + k)] = 1;
        const auto l = lift97(x);
        const double tap = h1[static_cast<std::size_t>(static_cast<int>(centre1) + k)];
        worst_hi_plus = std::max(worst_hi_plus, std::abs(l.d[16] - tap));
        worst_hi_minus = std::max(worst_hi_minus, std::abs(l.d[16] + tap));
    }
    CHECK(worst_lo <= 1e-12);
    CHECK(std::min(worst_hi_plus, worst_hi_minus) <= 1e-11);
}
