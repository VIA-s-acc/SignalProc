#include <cmath>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/check.hpp"
#include "core/design/biorthogonal.hpp"
#include "core/metrics.hpp"
#include "core/scalar.hpp"
#include "core/wavedec.hpp"
#include "support/banks.hpp"

using namespace signals::core;

namespace {

std::vector<Rational> integer_signal(std::size_t n) {
    std::mt19937 rng(11);
    std::uniform_int_distribution<int> d(-1000, 1000);
    std::vector<Rational> x;
    for (std::size_t i = 0; i < n; ++i) x.emplace_back(d(rng));
    return x;
}

}  // namespace

TEST_CASE("rational haar round trip is bit exact", "[exact]") {
    auto b = *make_bank<Rational>("haar-sum", BasicLaurent<Rational>{{Rational{1, 2}, Rational{1, 2}}, 0},
                                  BasicLaurent<Rational>{{Rational{1}, Rational{1}}, 0});
    std::vector<Rational> x{4, 6, 10, 12, 8, 6, 5, 5};
    auto c = *wavedec<Rational>(x, b, 3, {});
    CHECK(*waverec<Rational>(c, b, {}) == x);
}

TEST_CASE("rational spline cdf round trips are bit exact in all modes", "[exact]") {
    for (auto [nr, nd] : {std::pair{1, 3}, std::pair{2, 2}, std::pair{2, 4}, std::pair{3, 3}}) {
        auto b = *cdf_spline<Rational>(nr, nd);
        for (auto mode : signals::test::all_modes)
            for (std::size_t n : {32, 33}) {
                const auto x = integer_signal(n);
                const auto levels = dwt_max_level(n, b.length());
                INFO(b.name << " " << to_string(mode) << " n=" << n << " J=" << levels);
                REQUIRE(levels >= 1);
                auto c = wavedec<Rational>(x, b, levels, {mode});
                REQUIRE(c);
                auto y = waverec<Rational>(*c, b, {mode});
                REQUIRE(y);
                CHECK(*y == x);
            }
    }
}

TEST_CASE("double dwt agrees with rational reference", "[exact]") {
    const auto rb = *cdf_spline<Rational>(2, 2);
    const auto db = *cdf_spline<double>(2, 2);
    const auto xr = integer_signal(64);
    std::vector<double> xd;
    for (const auto& v : xr) xd.push_back(static_cast<double>(v));
    const std::size_t levels = dwt_max_level(64, db.length());
    const auto cr = *wavedec<Rational>(xr, rb, levels, {});
    const auto cd = *wavedec<double>(xd, db, levels, {});
    REQUIRE(cr.bands.size() == cd.bands.size());
    const double tol = default_tolerance(levels, db.length(), signals::test::max_abs(xd));
    for (std::size_t k = 0; k < cd.bands.size(); ++k) {
        const double exponent = k == 0 ? static_cast<double>(levels) / 2.0
                                       : (static_cast<double>(levels - k + 1) - 2.0) / 2.0;
        const double factor = std::pow(2.0, exponent);
        std::vector<double> scaled;
        for (const auto& v : cr.bands[k]) scaled.push_back(static_cast<double>(v) * factor);
        INFO("band " << k);
        CHECK(max_abs_error(cd.bands[k], scaled) <= tol);
    }
}

TEST_CASE("exact division by 1 + w checks the remainder", "[exact]") {
    using signals::core::Rational;
    const signals::core::BasicLaurent<Rational> divisible{{Rational{1}, Rational{2}, Rational{1}}, 0};
    auto q = signals::core::detail::divide_exactly_by_one_plus_w(divisible);
    REQUIRE(q);
    CHECK(q->coeffs == std::vector<Rational>{Rational{1}, Rational{1}});
    const signals::core::BasicLaurent<Rational> not_divisible{{Rational{1}, Rational{1}, Rational{1}}, 0};
    CHECK(signals::core::detail::divide_exactly_by_one_plus_w(not_divisible).error().code == signals::core::Errc::invalid_argument);
}
