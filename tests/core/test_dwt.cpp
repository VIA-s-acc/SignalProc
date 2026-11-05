#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include "core/check.hpp"
#include "core/dwt.hpp"
#include "core/metrics.hpp"
#include "support/banks.hpp"
#include "support/golden.hpp"

using namespace signals::core;
using signals::test::golden_bank;
using signals::test::max_abs;

TEST_CASE("dwt matches pywt for every wavelet, mode and length", "[dwt][golden]") {
    auto g = signals::test::load_golden("dwt.json");
    for (auto& c : g["cases"]) {
        auto x = signals::test::golden_signal(g, c["n"].get<std::size_t>());
        auto b = golden_bank(c["wavelet"].get<std::string>());
        DwtOptions o{*parse_extension(c["mode"].get<std::string>()), true};
        auto r = dwt_level<double>(x, b, o);
        INFO(c["wavelet"] << " " << c["mode"] << " n=" << c["n"]);
        if (c.contains("error")) {
            REQUIRE_FALSE(r);
            CHECK(r.error().code == Errc::invalid_argument);
            continue;
        }
        REQUIRE(r);
        double tol = 1e-12 * static_cast<double>(b.length()) * (1.0 + max_abs(x));
        CHECK(max_abs_error(r->approx, c["cA"].get<std::vector<double>>()) <= tol);
        CHECK(max_abs_error(r->detail, c["cD"].get<std::vector<double>>()) <= tol);
    }
}

TEST_CASE("default detail sign is the negated pywt sign", "[dwt]") {
    auto b = golden_bank("haar");
    std::vector<double> x{4, 6, 10, 12, 8, 6, 5, 5};
    auto sn = dwt_level<double>(x, b, {}), pw = dwt_level<double>(x, b, {Extension::symmetric, true});
    REQUIRE(sn);
    REQUIRE(pw);
    for (std::size_t k = 0; k < 4; ++k) CHECK(sn->detail[k] == -pw->detail[k]);
    CHECK(sn->detail[0] == Catch::Approx(std::numbers::sqrt2));
}

TEST_CASE("single level round trip is perfect", "[dwt][property]") {
    CHECK(rc::check("idwt(dwt(x)) == x", [] {
        const auto x = *rc::gen::container<std::vector<double>>(
            rc::gen::map(rc::gen::inRange(-1'000'000, 1'000'000), [](int v) { return static_cast<double>(v); }));
        const auto wi = *rc::gen::arbitrary<std::uint8_t>();
        const auto mi = *rc::gen::arbitrary<std::uint8_t>();
        RC_PRE(x.size() >= 2 && x.size() < 300);
        RC_ASSERT(signals::test::round_trip_error(x, wi, mi) <=
                  default_tolerance(1, 20, 1.0 + max_abs(x)) + 1e-10 * (1.0 + max_abs(x)));
    }));
}

TEST_CASE("empty input and mismatched lengths are errors", "[dwt]") {
    auto b = golden_bank("haar");
    CHECK(dwt_level<double>(std::span<const double>{}, b, {}).error().code == Errc::invalid_argument);
    std::vector<double> a{1, 2}, d{1};
    CHECK(idwt_level<double>(a, d, b, {}).error().code == Errc::length_mismatch);
}
