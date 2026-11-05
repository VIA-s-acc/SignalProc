#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/check.hpp"
#include "core/metrics.hpp"

using namespace signals::core;

TEST_CASE("compensated energy survives cancellation", "[metrics]") {
    std::vector<double> x(10'000'000, 1e-4);
    x[0] = 1e4;
    CHECK(std::abs(energy(x) - (1e8 + 1e7 * 1e-8)) < 1e-6);
}

TEST_CASE("nan never passes a check", "[metrics]") {
    CheckResult r{"check.pr_error", "db2/symmetric", std::nan(""), 1e-12};
    CHECK_FALSE(r.passed());
    std::vector<double> a{1, std::nan("")}, b{1, 2};
    CHECK(std::isnan(max_abs_error(a, b)));
}

TEST_CASE("default tolerance formula", "[metrics]") {
    CHECK(default_tolerance(6, 4, 1.0) == Catch::Approx(100.0 * 6 * 4 * std::ldexp(1.0, -53)));
    CHECK(default_tolerance(0, 4, 1.0) == default_tolerance(1, 4, 1.0));
}

TEST_CASE("snr of identical signals is infinite", "[metrics]") {
    std::vector<double> x{1, -1, 2};
    CHECK(std::isinf(snr_db(x, x)));
}

TEST_CASE("error metrics on known vectors", "[metrics]") {
    std::vector<double> a{1, 2, 3}, b{1, 2, 5};
    CHECK(max_abs_error(a, b) == 2.0);
    CHECK(rms_error(a, b) == Catch::Approx(std::sqrt(4.0 / 3.0)));
    CHECK(std::isinf(max_abs_error(a, std::vector<double>{1, 2})));
    CHECK(relative_l2_error(a, b) == Catch::Approx(2.0 / std::sqrt(14.0)));
}
