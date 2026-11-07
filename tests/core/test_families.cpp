#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/wavelet/regularity.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("all 106 wavelets match pywt filters", "[families][golden]") {
    const auto golden = signals::test::load_golden("filters.json");
    for (auto& w : golden["wavelets"]) {
        std::string name = w["name"];
        auto b = wavelet(name);
        INFO(name);
        REQUIRE(b);
        double tol = name.starts_with("sym") ? 1e-10 : 1e-12;
        CHECK(max_abs_error(b->h0.coeffs, w["dec_lo"].get<std::vector<double>>()) <= tol);
        CHECK(max_abs_error(b->f0.coeffs, w["rec_lo"].get<std::vector<double>>()) <= tol);
        if (!w["vanishing_moments_psi"].is_null())
            CHECK(wavelet_info(name)->vanishing_moments_psi == w["vanishing_moments_psi"].get<int>());
    }
    CHECK(wavelet_names().size() == 106);
    CHECK(wavelet_names().front() == "bior1.1");
}

TEST_CASE("malformed wavelet names are rejected", "[families]") {
    for (auto n : {"db0", "db39", "db02", "sym1", "sym21", "coif18", "bior2.3", "rbio4.5", "DB2", " db2", "db2 ", ""})
        CHECK(wavelet(n).error().code == Errc::unknown_wavelet);
}

TEST_CASE("generated flag is honest", "[families]") {
    CHECK(wavelet_info("db10")->generated);
    CHECK(wavelet_info("bior2.2")->generated);
    CHECK(wavelet_info("rbio3.3")->generated);
    CHECK_FALSE(wavelet_info("coif3")->generated);
    CHECK_FALSE(wavelet_info("dmey")->generated);
    CHECK_FALSE(wavelet_info("sym11")->generated);
    CHECK_FALSE(wavelet_info("bior4.4")->generated);
}

TEST_CASE("vanishing moment formulas agree with zeros at minus one for small orders", "[families]") {
    for (auto n : {"db2", "db5", "sym4", "coif1", "coif2", "bior2.2", "bior3.5"})
        CHECK(zeros_at_minus_one(wavelet(n)->f0, 1e-8) == *wavelet_info(n)->vanishing_moments_psi);
}

TEST_CASE("rational spline uses the summing normalisation", "[families]") {
    auto b = cdf_spline<Rational>(2, 2);
    REQUIRE(b);
    Rational h{0}, f{0};
    for (auto& c : b->h0.coeffs) h += c;
    for (auto& c : b->f0.coeffs) f += c;
    CHECK(h == 1);
    CHECK(f == 2);
}

TEST_CASE("regularity is consistent across all 106 wavelets", "[families][regularity]") {
    for (auto name : wavelet_names()) {
        const auto info = *wavelet_info(name);
        const auto r = wavelet_regularity(name);
        INFO(name);
        REQUIRE(r);
        if (name == "dmey") {
            CHECK_FALSE(r->zeros_f0.has_value());
            continue;
        }
        if (!name.starts_with("rbio")) CHECK(r->zeros_f0 == info.vanishing_moments_psi);
        if (info.orthogonal) {
            CHECK(r->zeros_h0 == r->zeros_f0);
            if (r->sobolev_h0 && r->sobolev_f0) CHECK(std::abs(*r->sobolev_h0 - *r->sobolev_f0) < 1e-6);
        }
    }
}

TEST_CASE("daubechies smoothness grows with order up to db38", "[families][regularity]") {
    double previous = 0.0;
    for (int n = 1; n <= 38; ++n) {
        const auto r = *wavelet_regularity("db" + std::to_string(n));
        INFO("db" << n);
        REQUIRE(r.sobolev_f0);
        CHECK(*r.sobolev_f0 > previous);
        previous = *r.sobolev_f0;
    }
    CHECK(*wavelet_regularity("db2")->sobolev_f0 == Catch::Approx(1.0).margin(1e-9));
    CHECK(*wavelet_regularity("bior2.2")->sobolev_f0 == Catch::Approx(1.5).margin(1e-9));
    CHECK(*wavelet_regularity("bior2.2")->sobolev_h0 == Catch::Approx(0.44).margin(0.01));
}

TEST_CASE("a user bank gets its info and regularity from its own coefficients", "[core][families][user_banks]") {
    const auto db3 = signals::core::wavelet("db3");
    REQUIRE(db3);
    signals::core::set_user_wavelets([db3](std::string_view name) -> std::optional<signals::core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db3;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    const auto info = signals::core::wavelet_info("my_bank");
    REQUIRE(info);
    CHECK(info->family == "user");
    CHECK(info->vanishing_moments_psi == 3);
    const auto reg = signals::core::wavelet_regularity("my_bank");
    REQUIRE(reg);
    CHECK(reg->zeros_h0 == 3);
    CHECK(reg->zeros_f0 == 3);
    REQUIRE(reg->sobolev_h0);
    const auto builtin = signals::core::wavelet_regularity("db3");
    CHECK(*reg->sobolev_h0 == Catch::Approx(*builtin->sobolev_h0).margin(1e-3));
    signals::core::set_user_wavelets({}, {});
    CHECK_FALSE(signals::core::wavelet_info("my_bank"));
}
