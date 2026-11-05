#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/metrics.hpp"
#include "core/wavedec.hpp"
#include "support/banks.hpp"
#include "support/golden.hpp"

using namespace signals::core;
using signals::test::golden_bank;
using signals::test::max_abs;
using signals::test::signal_of_length;

TEST_CASE("wavedec and waverec match pywt", "[wavedec][golden]") {
    auto g = signals::test::load_golden("wavedec.json");
    for (auto& c : g["cases"]) {
        const auto n = c["n"].get<std::size_t>();
        auto x = g["signals"][std::to_string(n)].get<std::vector<double>>();
        auto b = golden_bank(c["wavelet"].get<std::string>());
        DwtOptions o{*parse_extension(c["mode"].get<std::string>()), true};
        INFO(c["wavelet"] << " " << c["mode"] << " n=" << n << " level=" << c["level"]);
        CHECK(dwt_max_level(n, b.length()) == c["max_level"].get<std::size_t>());
        auto r = wavedec<double>(x, b, c["level"].get<std::size_t>(), o);
        REQUIRE(r);
        REQUIRE(r->bands.size() == c["coeffs"].size());
        const double tol = 1e-12 * static_cast<double>(b.length()) * (1.0 + max_abs(x)) * c["level"].get<double>();
        for (std::size_t k = 0; k < r->bands.size(); ++k)
            CHECK(max_abs_error(r->bands[k], c["coeffs"][k].get<std::vector<double>>()) <= tol);
        auto y = waverec<double>(*r, b, o);
        REQUIRE(y);
        REQUIRE(y->size() == n);
        if (!c.contains("waverec") && c["wavelet"] == "dmey") continue;
        std::vector<double> expected = c.contains("waverec") ? c["waverec"].get<std::vector<double>>() : x;
        expected.resize(n);
        CHECK(max_abs_error(*y, expected) <= tol + 1e-10 * (1.0 + max_abs(x)));
    }
}

TEST_CASE("odd length round trip returns exactly n samples", "[wavedec]") {
    auto db2 = golden_bank("db2");
    auto c = wavedec<double>(signal_of_length(17), db2, 2, {});
    REQUIRE(c);
    auto y = waverec<double>(*c, db2, {});
    REQUIRE(y);
    CHECK(y->size() == 17);
}

TEST_CASE("level bounds are enforced", "[wavedec]") {
    auto db2 = golden_bank("db2");
    CHECK(dwt_max_level(3, 4) == 0);
    CHECK(wavedec<double>(signal_of_length(3), db2, 1, {}).error().code == Errc::level_out_of_range);
    CHECK(wavedec<double>(signal_of_length(64), db2, 0, {}).error().code == Errc::level_out_of_range);
    CHECK(dwt_max_level(1000, 6) == 7);
}

TEST_CASE("haar walkthrough coefficients from the textbook", "[wavedec]") {
    std::vector<double> x{4, 6, 10, 12, 8, 6, 5, 5};
    auto c = wavedec<double>(x, golden_bank("haar"), 3, {});
    REQUIRE(c);
    CHECK(c->bands[0][0] == Catch::Approx(28 / std::numbers::sqrt2));
    CHECK(c->bands[1][0] == Catch::Approx(-4 / std::numbers::sqrt2));
    REQUIRE(c->bands[2].size() == 2);
    CHECK(c->bands[2][0] == Catch::Approx(6.0));
    CHECK(c->bands[2][1] == Catch::Approx(-2.0));
}

TEST_CASE("parseval holds for orthogonal periodization", "[wavedec]") {
    auto x = signal_of_length(1024);
    auto c = wavedec<double>(x, golden_bank("db2"), 6, {Extension::periodization, false});
    REQUIRE(c);
    double e = 0;
    for (auto& b : c->bands) e += energy(b);
    CHECK(std::abs(e - energy(x)) <= 1e-14 * energy(x));
}

TEST_CASE("repeated calls are independent", "[wavedec]") {
    auto a = wavedec<double>(signal_of_length(64), golden_bank("db2"), 2, {});
    auto b = wavedec<double>(signal_of_length(32), golden_bank("db2"), 2, {});
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->bands.size() == 3);
    CHECK(b->bands.size() == 3);
    CHECK(a->original_length == 64);
    CHECK(b->original_length == 32);
}

TEST_CASE("mismatched band lengths are rejected", "[wavedec]") {
    auto db2 = golden_bank("db2");
    auto c = *wavedec<double>(signal_of_length(64), db2, 2, {});
    c.bands[1].pop_back();
    c.bands[1].pop_back();
    CHECK(waverec<double>(c, db2, {}).error().code == Errc::length_mismatch);
}
