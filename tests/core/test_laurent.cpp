#include <complex>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/laurent.hpp"
#include "core/scalar.hpp"

using signals::core::Laurent;

TEST_CASE("multiply keeps interior and edge zeros", "[laurent]") {
    Laurent a{{1, 0, -1}, 0}, b{{0, 2}, -1};
    auto c = multiply(a, b);
    CHECK(c.offset == -1);
    CHECK(c.coeffs == std::vector<double>{0, 2, 0, -2});
}

TEST_CASE("alternate uses absolute index", "[laurent]") {
    Laurent p{{1, 1}, 1};
    CHECK(alternate(p) == Laurent{{-1, 1}, 1});
}

TEST_CASE("reverse maps support and leaves argument intact", "[laurent]") {
    const Laurent p{{1, 2, 3}, 2};
    auto r = reverse(p);
    CHECK(r == Laurent{{3, 2, 1}, -4});
    CHECK(p == Laurent{{1, 2, 3}, 2});
}

TEST_CASE("haar halfband difference is a monomial", "[laurent]") {
    Laurent p0 = scale(power(Laurent{{1, 1}, 0}, 2), 0.5);
    auto m = as_monomial(subtract(p0, alternate(p0)), 1e-15);
    REQUIRE(m);
    CHECK(m->coef == 2.0);
    CHECK(m->exponent == 1);
}

TEST_CASE("evaluate matches closed form", "[laurent]") {
    auto h = evaluate(Laurent{{1, 1}, 0}, std::numbers::pi / 2);
    CHECK(std::abs(h - std::complex<double>{1, -1}) < 1e-15);
}

TEST_CASE("rational arithmetic is exact", "[laurent]") {
    using signals::core::BasicLaurent, signals::core::Rational;
    BasicLaurent<Rational> q{{Rational{1, 3}, Rational{2, 3}}, 0};
    CHECK(multiply(q, q).coeffs[1] == Rational{4, 9});
}

TEST_CASE("trim keeps polynomial and moves offset", "[laurent]") {
    auto t = trim(Laurent{{0, 0, 1, 0, 2, 0}, -1}, 0.0);
    CHECK(t == Laurent{{1, 0, 2}, 1});
    CHECK(trim(Laurent{{0, 0}, 3}, 0.0).coeffs.empty());
}

TEST_CASE("shift and at follow the z^-n convention", "[laurent]") {
    auto s = shift(Laurent{{1, 2}, 0}, 3);
    CHECK(s.first() == 3);
    CHECK(s.last() == 4);
    CHECK(s.at(4) == 2.0);
    CHECK(s.at(0) == 0.0);
}
