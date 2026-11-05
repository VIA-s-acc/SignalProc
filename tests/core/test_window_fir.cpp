#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "core/design/window_fir.hpp"
#include "core/laurent.hpp"
#include "core/metrics.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("kaiser design matches scipy", "[fir][golden]") {
    const auto golden = signals::test::load_golden("kaiser.json");
    for (auto& c : golden["kaiserord"]) {
        INFO("kaiserord A=" << c["attenuation"] << " width=" << c["width"]);
        auto o = kaiser_order(c["attenuation"], c["width"]);
        CHECK(o.taps == c["taps"].get<std::size_t>());
        CHECK(std::abs(o.beta - c["beta"].get<double>()) <= 1e-12);
    }
    for (auto& c : golden["kaiser_beta"]) {
        INFO("kaiser_beta A=" << c["attenuation"]);
        CHECK(std::abs(kaiser_beta(c["attenuation"]) - c["beta"].get<double>()) <= 1e-12);
    }
    for (auto& c : golden["firwin"]) {
        INFO("firwin taps=" << c["taps"]);
        auto h = kaiser_lowpass(c["taps"].get<std::size_t>(), c["cutoff"], c["beta"]);
        CHECK(h.offset == 0);
        CHECK(max_abs_error(h.coeffs, c["h"].get<std::vector<double>>()) <= 1e-14);
    }
}

TEST_CASE("kaiser lowpass meets its attenuation", "[fir]") {
    auto [taps, beta] = kaiser_order(80, 0.05);
    auto h = kaiser_lowpass(taps | 1, 0.25, beta);
    double stop_db = -1000;
    for (std::size_t k = 0; k < 4096; ++k) {
        const double omega = std::numbers::pi * static_cast<double>(k) / 4096;
        if (omega >= 0.30 * std::numbers::pi) stop_db = std::max(stop_db, 20 * std::log10(std::abs(evaluate(h, omega))));
    }
    CHECK(stop_db <= -79.5);
    CHECK(std::abs(std::abs(evaluate(h, 0.0)) - 1.0) <= 1e-12);
}

TEST_CASE("degenerate kaiser lengths", "[fir]") {
    CHECK(kaiser_lowpass(1, 0.5, 5.0).coeffs == std::vector<double>{1.0});
    CHECK(kaiser_lowpass(0, 0.5, 5.0).coeffs.empty());
    CHECK(kaiser_beta(10) == 0.0);
}
