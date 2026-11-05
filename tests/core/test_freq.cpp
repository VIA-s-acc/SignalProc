#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "core/design/families.hpp"
#include "core/freq.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("frequency response matches scipy freqz and group_delay", "[freq][golden]") {
    const auto golden = signals::test::load_golden("freqz.json");
    for (auto& c : golden["cases"]) {
        INFO(c["name"]);
        auto r = frequency_response(Laurent{c["b"].get<std::vector<double>>(), 0}, 512);
        const auto re = c["h_re"].get<std::vector<double>>(), im = c["h_im"].get<std::vector<double>>();
        const auto gd = c["gd"].get<std::vector<double>>();
        REQUIRE(r.h.size() == 512);
        const double peak = *std::max_element(r.magnitude.begin(), r.magnitude.end());
        double h_err = 0, gd_err = 0, gd_scaled_err = 0;
        for (std::size_t k = 0; k < 512; ++k) {
            CHECK(r.omega[k] == std::numbers::pi * static_cast<double>(k) / 512);
            h_err = std::max(h_err, std::abs(r.h[k] - std::complex<double>{re[k], im[k]}));
            const double diff = std::abs(r.group_delay[k] - gd[k]);
            if (r.magnitude[k] > 1e-3 * peak) gd_err = std::max(gd_err, diff);
            if (r.magnitude[k] > 1e-6 * peak) gd_scaled_err = std::max(gd_scaled_err, diff * r.magnitude[k] / peak);
        }
        CHECK(h_err <= 1e-13);
        CHECK(gd_err <= 1e-9);
        CHECK(gd_scaled_err <= 2e-12);
    }
}

TEST_CASE("db2 lowpass is -3 dB at half band and power complementary", "[freq]") {
    auto b = *wavelet("db2");
    auto r0 = frequency_response(b.h0, 512), r1 = frequency_response(b.h1, 512);
    CHECK(r0.magnitude_db[256] == Catch::Approx(0.0).margin(1e-12));
    for (std::size_t k = 0; k < 512; ++k)
        CHECK(r0.magnitude[k] * r0.magnitude[k] + r1.magnitude[k] * r1.magnitude[k] == Catch::Approx(2.0).margin(1e-13));
    CHECK(std::isnan(r1.group_delay[0]));
    CHECK(r1.magnitude_db[0] == -300.0);
}

TEST_CASE("unwrapped phase of a pure delay is a straight line", "[freq]") {
    auto r = frequency_response(Laurent{{0.0, 0.0, 0.0, 0.0, 0.0, 1.0}, 0}, 512);
    for (std::size_t k = 0; k < 512; ++k) {
        CHECK(r.phase_unwrapped[k] == Catch::Approx(-5 * r.omega[k]).margin(1e-12));
        CHECK(r.group_delay[k] == Catch::Approx(5.0).margin(1e-12));
    }
}

TEST_CASE("db2 zeros and step response", "[freq]") {
    auto zp = *zeros_poles(wavelet("db2")->f0);
    REQUIRE(zp.zeros.size() == 3);
    const auto near = [&](std::complex<double> z, double tol) {
        return std::count_if(zp.zeros.begin(), zp.zeros.end(), [&](auto r) { return std::abs(r - z) <= tol; });
    };
    CHECK(near(-1.0, 1e-6) == 2);
    CHECK(near(2 - std::sqrt(3.0), 1e-12) == 1);
    CHECK(zp.poles_at_origin == 3);
    auto s = step_response(wavelet("db2")->f0);
    CHECK(std::abs(s.back() - std::numbers::sqrt2) <= 1e-15);
    CHECK(zeros_poles(Laurent{{}, 0}).error().code == Errc::invalid_argument);
    CHECK(zeros_poles(Laurent{{2.0}, -1})->poles_at_origin == -1);
}
