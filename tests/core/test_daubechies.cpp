#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/daubechies.hpp"
#include "core/metrics.hpp"
#include "core/wavelet/regularity.hpp"
#include "support/banks.hpp"

using namespace signals::core;
using signals::test::golden_wavelet;

TEST_CASE("daubechies match pywt", "[families][golden]") {
    for (int n = 1; n <= 38; ++n) {
        auto b = daubechies(n);
        REQUIRE(b);
        auto name = "db" + std::to_string(n);
        INFO(name);
        CHECK(max_abs_error(b->f0.coeffs, golden_wavelet(name)["rec_lo"].get<std::vector<double>>()) <= 1e-12);
        CHECK(max_abs_error(b->h0.coeffs, golden_wavelet(name)["dec_lo"].get<std::vector<double>>()) <= 1e-12);
        CHECK(check_orthogonality(*b, 1e-13).passed());
    }
}

TEST_CASE("symlets match pywt", "[families][golden]") {
    for (int n = 2; n <= 20; ++n) {
        auto name = "sym" + std::to_string(n);
        auto b = symlet(n);
        REQUIRE(b);
        INFO(name);
        CHECK(max_abs_error(b->f0.coeffs, golden_wavelet(name)["rec_lo"].get<std::vector<double>>()) <= 1e-10);
    }
    INFO("symlets from the pywt table instead of generation: " << kSymletTableFallback.size());
    CHECK(kSymletTableFallback.size() <= 19);
}

TEST_CASE("db zeros at minus one equal order", "[families]") {
    for (int n : {2, 5, 10}) CHECK(zeros_at_minus_one(daubechies(n)->f0, 1e-6) == n);
}
