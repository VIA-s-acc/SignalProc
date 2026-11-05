#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "commands/suites.hpp"
#include "core/design/families.hpp"
#include "core/freq.hpp"
#include "core/metrics.hpp"

using namespace signals::core;

TEST_CASE("dilation spreads coefficients", "[freq]") {
    auto d = dilate(Laurent{{1, 2, 3}, -1}, 3);
    CHECK(d.offset == -3);
    CHECK(d.coeffs == std::vector<double>{1, 0, 0, 2, 0, 0, 3});
    CHECK(dilate(Laurent{{1, 2}, 0}, 1).coeffs == std::vector<double>{1, 2});
}

TEST_CASE("haar equivalent filters are boxes and box differences", "[freq]") {
    auto h = *wavelet("haar");
    const double s = std::pow(2.0, -1.5);
    auto a3 = *equivalent_filter(h, 3, false);
    REQUIRE(a3.coeffs.size() == 8);
    CHECK(max_abs_error(a3.coeffs, std::vector<double>(8, s)) <= 1e-15);
    auto d3 = *equivalent_filter(h, 3, true);
    CHECK(max_abs_error(d3.coeffs, std::vector<double>{s, s, s, s, -s, -s, -s, -s}) <= 1e-15);
    CHECK(equivalent_filter(h, 0, true).error().code == Errc::invalid_argument);
    CHECK(equivalent_filter(h, 1, false)->coeffs == h.h0.coeffs);
    CHECK(equivalent_filter(h, 1, true)->coeffs == h.h1.coeffs);
}

TEST_CASE("alias transfer vanishes for every generated bank", "[freq]") {
    for (const auto name : wavelet_names()) {
        const auto info = wavelet_info(name);
        if (!info || !info->generated) continue;
        INFO(name);
        const auto bank = *wavelet(name);
        const auto a = alias_transfer(bank);
        double worst = 0;
        for (double c : a.coeffs) worst = std::max(worst, std::abs(c));
        CHECK(worst <= 1e-14);
        CHECK(as_monomial(distortion_transfer(bank), 1e-12).has_value());
    }
}

TEST_CASE("band metrics of db2 and the d2 octave", "[freq]") {
    auto b = *wavelet("db2");
    auto m = band_metrics(frequency_response(b.h0, 4096), 0.25, 0.75);
    CHECK(m.cutoff_3db == Catch::Approx(0.5).margin(1e-4));
    CHECK(m.passband_ripple_db >= 0.0);
    CHECK(m.passband_ripple_db < 3.0103);
    CHECK(m.stopband_attenuation_db > 3.0103);
    CHECK(m.transition_width > 0.0);
    auto r2 = frequency_response(*equivalent_filter(b, 2, true), 4096);
    const auto peak = std::max_element(r2.magnitude.begin(), r2.magnitude.end()) - r2.magnitude.begin();
    CHECK(r2.omega[peak] / std::numbers::pi >= 0.25);
    CHECK(r2.omega[peak] / std::numbers::pi <= 0.5);
}

TEST_CASE("power complementarity is part of the core suite", "[freq]") {
    const auto results = signals::cli::core_suite();
    std::size_t count = 0;
    for (const auto& r : results) {
        if (r.id != "check.power_complementary") continue;
        ++count;
        INFO(r.subject << " " << r.value << " > " << r.tolerance);
        CHECK(r.passed());
    }
    CHECK(count >= 70);
}
