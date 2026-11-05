#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/wavelet/regularity.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("all 106 wavelets match pywt filters", "[families][golden]") {
    for (auto& w : signals::test::load_golden("filters.json")["wavelets"]) {
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
