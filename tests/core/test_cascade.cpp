#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numeric>

#include "core/design/families.hpp"
#include "core/wavelet/cascade.hpp"

using namespace signals::core;

TEST_CASE("cascade approaches the db2 scaling function", "[core][cascade]") {
    auto c = cascade(wavelet("db2")->f0, 14);
    REQUIRE(c);
    CHECK(c->dt == 1.0 / 16384);
    CHECK(c->support == 3);
    CHECK(std::abs(c->phi[16384] - (1 + std::sqrt(3.0)) / 2) < 0.01);
    CHECK(std::abs(c->phi[2 * 16384] - (1 - std::sqrt(3.0)) / 2) < 0.01);
    CHECK(std::accumulate(c->phi.begin(), c->phi.end(), 0.0) * c->dt == Catch::Approx(1).margin(1e-12));
    auto eight = cascade(wavelet("db2")->f0, 8);
    REQUIRE(eight);
    CHECK(eight->norm_ratio < 1.02);
}

TEST_CASE("cascade of haar is the box", "[core][cascade]") {
    auto c = cascade(wavelet("haar")->f0, 6);
    REQUIRE(c);
    CHECK(c->phi.size() == 64);
    for (double v : c->phi) CHECK(v == Catch::Approx(1));
}

TEST_CASE("cascade of a filter without zeros at -1 diverges", "[core][cascade]") {
    auto c = cascade(Laurent{{1.0}, 0}, 8);
    REQUIRE(c);
    CHECK(c->norm_ratio == Catch::Approx(std::sqrt(2.0)).margin(1e-12));
}

TEST_CASE("cascade rejects a zero-sum filter and too few iterations", "[core][cascade]") {
    CHECK_FALSE(cascade(Laurent{{1.0, -1.0}, 0}, 8));
    CHECK_FALSE(cascade(wavelet("db2")->f0, 1));
}
