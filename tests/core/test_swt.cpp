#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core/metrics.hpp"
#include "core/swt.hpp"
#include "support/banks.hpp"
#include "support/golden.hpp"

using namespace signals::core;
using signals::test::golden_bank;
using signals::test::max_abs;
using signals::test::signal_of_length;

TEST_CASE("swt, iswt and mra match pywt", "[swt][golden]") {
    auto g = signals::test::load_golden("swt.json");
    for (auto& c : g["cases"]) {
        const auto n = c["n"].get<std::size_t>();
        const auto x = g["signals"][std::to_string(n)].get<std::vector<double>>();
        const auto b = golden_bank(c["wavelet"].get<std::string>());
        const auto level = c["level"].get<std::size_t>();
        const SwtOptions o{c["norm"].get<bool>(), true};
        INFO(c["wavelet"] << " n=" << n << " level=" << level << " norm=" << o.norm);
        CHECK(swt_max_level(n) == c["max_level"].get<std::size_t>());
        const auto r = swt<double>(x, b, level, o);
        REQUIRE(r);
        REQUIRE(r->bands.size() == c["coeffs"].size());
        const double tol = 1e-12 * static_cast<double>(b.length()) * (1.0 + max_abs(x)) * static_cast<double>(level + 1);
        for (std::size_t k = 0; k < r->bands.size(); ++k) CHECK(max_abs_error(r->bands[k], c["coeffs"][k].get<std::vector<double>>()) <= tol);
        const auto y = iswt<double>(*r, b, o);
        REQUIRE(y);
        CHECK(max_abs_error(*y, c["iswt"].get<std::vector<double>>()) <= tol);
        CHECK(max_abs_error(*y, x) <= tol);
        if (!c.contains("mra")) continue;
        const auto m = mra<double>(x, b, level, o);
        REQUIRE(m);
        REQUIRE(m->size() == c["mra"].size());
        std::vector<double> sum(n, 0.0);
        for (std::size_t k = 0; k < m->size(); ++k) {
            CHECK(max_abs_error((*m)[k], c["mra"][k].get<std::vector<double>>()) <= tol);
            for (std::size_t i = 0; i < n; ++i) sum[i] += (*m)[k][i];
        }
        CHECK(max_abs_error(sum, x) <= tol);
    }
}

TEST_CASE("modwt keeps the energy of an orthogonal bank", "[swt]") {
    const auto x = signal_of_length(256);
    const auto b = golden_bank("db4");
    const auto c = swt<double>(x, b, 5, {true, true});
    REQUIRE(c);
    double e = 0, ex = 0;
    for (const auto& band : c->bands)
        for (const double v : band) e += v * v;
    for (const double v : x) ex += v * v;
    CHECK(std::abs(e - ex) <= 1e-12 * ex);
}

TEST_CASE("swt refuses a length that is not a multiple of 2^J", "[swt]") {
    const auto x = signal_of_length(100);
    CHECK(swt_max_level(100) == 2);
    const auto r = swt<double>(x, golden_bank("db2"), 3, {});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::level_out_of_range);
    CHECK(swt<double>(x, golden_bank("db2"), 2, {}));
}
