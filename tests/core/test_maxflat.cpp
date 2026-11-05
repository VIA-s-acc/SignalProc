#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/maxflat.hpp"
#include "core/design/roots.hpp"
#include "core/laurent.hpp"
#include "core/scalar.hpp"

using namespace signals::core;

namespace {

bool contains_root(const std::vector<Complex<Float256>>& roots, Complex<double> want, double tol) {
    return std::ranges::any_of(roots, [&](const auto& r) {
        return std::hypot(static_cast<double>(r.re) - want.re, static_cast<double>(r.im) - want.im) < tol;
    });
}

}  // namespace

TEST_CASE("p = 2 reproduces the old SignalProc product filter", "[maxflat]") {
    auto p0 = maxflat_halfband<Rational>(2);
    CHECK(p0.offset == 0);
    CHECK(p0.coeffs == std::vector<Rational>{Rational{-1, 16}, 0, Rational{9, 16}, 1, Rational{9, 16}, 0, Rational{-1, 16}});
}

TEST_CASE("maxflat is halfband exactly", "[maxflat]") {
    for (int p = 1; p <= 20; ++p) {
        auto p0 = maxflat_halfband<Rational>(p);
        auto d = subtract(p0, alternate(p0));
        INFO("p=" << p);
        CHECK(trim(d, Rational{0}) == BasicLaurent<Rational>{{Rational{2}}, 2 * p - 1});
    }
}

TEST_CASE("roots rebuild the product to 1e-13", "[maxflat]") {
    for (int p : {2, 3, 10, 20, 38}) {
        auto roots = maxflat_q_roots(p);
        INFO("p=" << p);
        REQUIRE(roots.size() == static_cast<std::size_t>(2 * p - 2));
        std::vector<Complex<Float256>> poly{{Float256{1}, Float256{0}}};
        auto times_linear = [&](Complex<Float256> r) {
            std::vector<Complex<Float256>> next(poly.size() + 1, {Float256{0}, Float256{0}});
            for (std::size_t i = 0; i < poly.size(); ++i) {
                next[i] = next[i] + poly[i];
                next[i + 1] = next[i + 1] - poly[i] * r;
            }
            poly = std::move(next);
        };
        for (int i = 0; i < 2 * p; ++i) times_linear({Float256{-1}, Float256{0}});
        for (const auto& r : roots) times_linear(r);
        Float256 sum{0};
        for (const auto& c : poly) sum += c.re;
        const auto expected = convert<double>(maxflat_halfband<Rational>(p));
        double peak = 0.0, worst = 0.0;
        for (double c : expected.coeffs) peak = std::max(peak, std::abs(c));
        for (std::size_t i = 0; i < poly.size(); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(poly[i].re * 2 / sum) - expected.coeffs[i]));
        CHECK(worst <= 1e-13 * peak);
    }
}

TEST_CASE("p = 3 roots match db3 zeros", "[maxflat]") {
    auto r = maxflat_q_roots(3);
    CHECK(contains_root(r, {0.2873, 0.1529}, 1e-4));
    CHECK(contains_root(r, {2.7127, -1.4439}, 1e-4));
}

TEST_CASE("aberth finds roots of a known polynomial", "[maxflat]") {
    auto roots = polynomial_roots<double>({-6, 11, -6, 1});
    REQUIRE(roots);
    std::vector<double> re;
    for (auto& z : *roots) re.push_back(z.re);
    std::ranges::sort(re);
    CHECK(std::abs(re[0] - 1) < 1e-12);
    CHECK(std::abs(re[1] - 2) < 1e-12);
    CHECK(std::abs(re[2] - 3) < 1e-12);
}
