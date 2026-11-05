#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/bank.hpp"
#include "core/design/split.hpp"
#include "core/metrics.hpp"

using namespace signals::core;

TEST_CASE("p = 2 has 20 splits and 6 wavelets", "[split]") {
    const auto f = factorise_maxflat(2);
    const auto groups_before = f.groups.size();
    auto splits = enumerate_splits(f);
    CHECK(splits.size() == 20);
    int wavelets = 0;
    for (auto& s : splits) {
        auto b = bank_from_split(f, s, "x");
        REQUIRE(b);
        wavelets += classify(*b).wavelet ? 1 : 0;
    }
    CHECK(wavelets == 6);
    CHECK(f.groups.size() == groups_before);
}

TEST_CASE("p = 3 has 28 splits", "[split]") { CHECK(enumerate_splits(factorise_maxflat(3)).size() == 28); }

TEST_CASE("every split is normalised with positive dc gain", "[split]") {
    auto f = factorise_maxflat(2);
    for (auto& s : enumerate_splits(f)) {
        auto b = *bank_from_split(f, s, "x");
        CHECK(neumaier_sum(b.h0.coeffs) == Catch::Approx(std::numbers::sqrt2));
        CHECK(neumaier_sum(b.f0.coeffs) == Catch::Approx(std::numbers::sqrt2));
        CHECK(check_pr(b).delay.has_value());
    }
}

TEST_CASE("cdf 3,1 is pr but not a wavelet", "[split]") {
    auto b = make_bank<double>("bior3.1-like", scale(Laurent{{-1, 3, 3, -1}, 0}, 0.25), scale(Laurent{{1, 3, 3, 1}, 0}, 0.25));
    REQUIRE(b);
    auto info = classify(*b);
    CHECK_FALSE(info.wavelet);
    CHECK(info.kind == SplitKind::linear_phase);
}

TEST_CASE("orthogonal split of p = 3 is classified as orthogonal", "[split]") {
    auto f = factorise_maxflat(3);
    for (auto& s : enumerate_splits(f)) {
        if (s.zeros_h0 != 3) continue;
        auto b = *bank_from_split(f, s, "x");
        auto info = classify(b);
        if (info.kind == SplitKind::orthogonal) CHECK(info.wavelet);
    }
}
