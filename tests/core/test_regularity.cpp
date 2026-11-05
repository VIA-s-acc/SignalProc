#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/wavelet/regularity.hpp"
#include "support/banks.hpp"

using namespace signals::core;

TEST_CASE("sobolev exponent known values", "[regularity]") {
    CHECK(sobolev_exponent(Laurent{{1, 1}, 0}) == Catch::Approx(0.5).margin(1e-9));
    CHECK(sobolev_exponent(Laurent{{1, 2, 1}, 0}) == Catch::Approx(1.5).margin(1e-9));
    CHECK(sobolev_exponent(Laurent{{1, 4, 6, 4, 1}, 0}) == Catch::Approx(3.5).margin(1e-9));
    const auto db2 = signals::test::golden_wavelet("db2")["rec_lo"].get<std::vector<double>>();
    CHECK(sobolev_exponent(Laurent{db2, 0}) == Catch::Approx(1.0).margin(1e-9));
    CHECK(sobolev_exponent(Laurent{{-1, 3, 3, -1}, 0}) == Catch::Approx(-0.56).margin(0.01));
    CHECK(sobolev_exponent(Laurent{{-1, 2, 6, 2, -1}, 0}) == Catch::Approx(0.44).margin(0.01));
    CHECK(sobolev_exponent(Laurent{{-1, 1, 8, 8, 1, -1}, 0}) == Catch::Approx(1.44).margin(0.01));
}

TEST_CASE("zeros at minus one are counted exactly", "[regularity]") {
    CHECK(zeros_at_minus_one(Laurent{{1, 4, 6, 4, 1}, 0}, 1e-12) == 4);
    CHECK(zeros_at_minus_one(Laurent{{1, -4, 1}, 0}, 1e-12) == 0);
    const auto db2 = signals::test::golden_wavelet("db2")["rec_lo"].get<std::vector<double>>();
    CHECK(zeros_at_minus_one(Laurent{db2, 0}, 1e-9) == 2);
}
