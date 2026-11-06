#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/scalar.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("haar bank has delay one", "[bank]") {
    const double r = std::numbers::sqrt2 / 2;
    auto b = make_bank<double>("haar", Laurent{{r, r}, 0}, Laurent{{r, r}, 0});
    REQUIRE(b);
    auto pr = check_pr(*b);
    CHECK(pr.delay == 1);
    CHECK(pr.alias.passed());
    CHECK(pr.distortion.passed());
}

TEST_CASE("legall 5/3 is laid out like pywt bior2.2", "[bank]") {
    auto b = make_bank<double>("legall", scale(Laurent{{-1, 2, 6, 2, -1}, 0}, 1.0 / 8), scale(Laurent{{1, 2, 1}, 0}, 0.5));
    REQUIRE(b);
    CHECK(b->length() == 6);
    CHECK(check_pr(*b).delay == 5);
    CHECK(b->h0.coeffs[0] == 0.0);
    CHECK(b->f0.coeffs[0] == 0.0);
}

TEST_CASE("dropping zero coefficients breaks reconstruction", "[bank]") {
    Laurent p0_without_zeros = scale(Laurent{{1, -9, -16, -9, 1}, 0}, -1.0 / 16);
    auto b = make_bank<double>("old#4", Laurent{{1}, 0}, p0_without_zeros);
    REQUIRE_FALSE(b);
    CHECK(b.error().code == Errc::not_perfect_reconstruction);
}

TEST_CASE("our convention equals pywt up to highpass sign", "[bank][golden]") {
    const auto golden = signals::test::load_golden("filters.json");
    for (auto& w : golden["wavelets"]) {
        const std::string name = w["name"];
        auto pw = bank_from_pywt<double>(name, w["dec_lo"], w["dec_hi"], w["rec_lo"], w["rec_hi"]);
        auto ours = make_bank<double>(name, pw.h0, pw.f0, name == "dmey" ? 5e-3 : 1e-10);
        INFO(name);
        REQUIRE(ours);
        CHECK(ours->h0 == pw.h0);
        CHECK(ours->f0 == pw.f0);
        CHECK(max_abs_error(ours->h1.coeffs, pw.h1.coeffs) < 1e-15);
        CHECK(max_abs_error(ours->f1.coeffs, pw.f1.coeffs) < 1e-15);
    }
}

TEST_CASE("orthogonality check separates db2 from bior2.2", "[bank][golden]") {
    auto f = signals::test::load_golden("filters.json")["wavelets"];
    auto find = [&](const std::string& n) {
        for (auto& w : f)
            if (w["name"] == n) return bank_from_pywt<double>(n, w["dec_lo"], w["dec_hi"], w["rec_lo"], w["rec_hi"]);
        throw std::runtime_error("missing " + n);
    };
    CHECK(check_orthogonality(find("db2"), 1e-13).passed());
    CHECK_FALSE(check_orthogonality(find("bior2.2"), 1e-13).passed());
    CHECK(is_linear_phase(find("bior2.2").h0, 1e-12));
    CHECK_FALSE(is_linear_phase(find("db2").h0, 1e-12));
}

TEST_CASE("rational banks are checked exactly", "[bank]") {
    BasicLaurent<Rational> h0{{Rational{1, 2}, Rational{1, 2}}, 0}, f0{{1, 1}, 0};
    auto b = make_bank<Rational>("haar-sum", h0, f0);
    REQUIRE(b);
    CHECK(b->f1 == BasicLaurent<Rational>{{Rational{-1, 2}, Rational{1, 2}}, 0});
}

TEST_CASE("nan in a filter never passes a check", "[bank]") {
    const double r = std::numbers::sqrt2 / 2;
    auto b = *make_bank<double>("haar", Laurent{{r, r}, 0}, Laurent{{r, r}, 0});
    b.h0.coeffs[1] = std::nan("");
    auto pr = check_pr(b);
    CHECK_FALSE(pr.alias.passed());
    CHECK_FALSE(pr.distortion.passed());
    CHECK_FALSE(pr.delay.has_value());
    CHECK_FALSE(check_orthogonality(b, 1e-12).passed());
}

TEST_CASE("make_bank rejects non-finite coefficients", "[bank]") {
    const double r = std::numbers::sqrt2 / 2;
    auto b = make_bank<double>("x", Laurent{{r, r}, 0}, Laurent{{r, r, 0, 0, std::nan("")}, 0});
    REQUIRE_FALSE(b);
    CHECK(b.error().code == Errc::invalid_argument);
    CHECK_FALSE(make_bank<double>("y", Laurent{{r, INFINITY}, 0}, Laurent{{r, r}, 0}));
}

TEST_CASE("trim keeps nan coefficients", "[laurent]") {
    auto t = trim(Laurent{{0, 1, std::nan("")}, 0}, 0.0);
    CHECK(t.coeffs.size() == 2);
}

TEST_CASE("vanishing moments of db2 h1 vanish to rounding", "[core][bank]") {
    const auto bank = signals::core::wavelet("db2");
    REQUIRE(bank);
    auto r = check_vanishing_moments(bank->h1, 2, 1e-12);
    CHECK(r.id == "check.vanishing_moments");
    CHECK(r.passed());
    auto three = check_vanishing_moments(bank->h1, 3, 1e-12);
    CHECK_FALSE(three.passed());
    CHECK(check_vanishing_moments(signals::core::wavelet("haar")->h1, 1, 1e-12).passed());
}
