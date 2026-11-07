#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "core/design/families.hpp"
#include "core/wavelet/spectra.hpp"

using namespace signals::core;

namespace {

constexpr double pi = std::numbers::pi;

std::vector<double> grid(double lo, double hi, std::size_t n) {
    std::vector<double> w(n);
    for (std::size_t k = 0; k < n; ++k) w[k] = lo + (hi - lo) * static_cast<double>(k) / static_cast<double>(n - 1);
    return w;
}

}  // namespace

TEST_CASE("haar scaling spectrum is the box transform", "[core][spectra]") {
    const auto bank = wavelet("haar");
    REQUIRE(bank);
    REQUIRE(bank->f0.offset == 0);
    const auto w = grid(-8 * pi, 8 * pi, 801);
    const auto phi = scaling_spectrum(bank->f0, w);
    REQUIRE(phi);
    for (std::size_t k = 0; k < w.size(); ++k) {
        const std::complex<double> expected = std::abs(w[k]) < 1e-12 ? std::complex<double>{1, 0}
                                                                     : (1.0 - std::polar(1.0, -w[k])) / std::complex<double>{0, w[k]};
        CHECK(std::abs((*phi)[k] - expected) < 1e-12);
    }
}

TEST_CASE("haar wavelet spectrum matches the transform of the step pair", "[core][spectra]") {
    const auto bank = wavelet("haar");
    REQUIRE(bank);
    const auto w = grid(0.01, 8 * pi, 400);
    const auto psi = wavelet_spectrum(bank->f0, bank->f1, w);
    REQUIRE(psi);
    for (std::size_t k = 0; k < w.size(); ++k) CHECK(std::abs(std::abs((*psi)[k]) - 4 * std::pow(std::sin(w[k] / 4), 2) / w[k]) < 1e-12);
}

TEST_CASE("orthogonal scaling spectrum is a partition of unity over 2πk shifts", "[core][spectra]") {
    const auto bank = wavelet("db4");
    REQUIRE(bank);
    std::vector<double> shifted;
    const std::vector<double> base{0.1, 0.7, 1.9, 3.0};
    constexpr int K = 400;
    for (double w : base)
        for (int k = -K; k <= K; ++k) shifted.push_back(w + 2 * pi * k);
    const auto phi = scaling_spectrum(bank->f0, shifted);
    REQUIRE(phi);
    for (std::size_t b = 0; b < base.size(); ++b) {
        double sum = 0;
        for (std::size_t k = 0; k < 2 * K + 1; ++k) sum += std::norm((*phi)[b * (2 * K + 1) + k]);
        CHECK(std::abs(sum - 1) < 1e-6);
    }
}

TEST_CASE("orthogonal wavelet spectrum tiles the dyadic octaves", "[core][spectra]") {
    const auto bank = wavelet("db3");
    REQUIRE(bank);
    for (double w : {0.3, 1.0, 2.5}) {
        std::vector<double> scaled;
        for (int j = -40; j <= 12; ++j) scaled.push_back(std::ldexp(w, j));
        const auto psi = wavelet_spectrum(bank->f0, bank->f1, scaled);
        REQUIRE(psi);
        double sum = 0;
        for (const auto& v : *psi) sum += std::norm(v);
        CHECK(std::abs(sum - 1) < 1e-6);
    }
}

TEST_CASE("scaling spectrum vanishes at nonzero multiples of 2π", "[core][spectra]") {
    for (const char* name : {"db2", "sym5", "bior2.2", "coif2"}) {
        const auto bank = wavelet(name);
        REQUIRE(bank);
        const std::vector<double> w{0.0, 2 * pi, 4 * pi, -2 * pi, 6 * pi};
        const auto phi = scaling_spectrum(bank->f0, w);
        REQUIRE(phi);
        CHECK(std::abs((*phi)[0] - 1.0) < 1e-12);
        for (std::size_t k = 1; k < w.size(); ++k) CHECK(std::abs((*phi)[k]) < 1e-10);
    }
}

TEST_CASE("truncated product keeps exactly the requested factors", "[core][spectra]") {
    const auto bank = wavelet("haar");
    REQUIRE(bank);
    const std::vector<double> w{1.3, 5.0};
    const auto two = scaling_spectrum(bank->f0, w, 2);
    REQUIRE(two);
    for (std::size_t k = 0; k < w.size(); ++k) {
        auto m0 = [](double x) { return (1.0 + std::polar(1.0, -x)) / 2.0; };
        CHECK(std::abs((*two)[k] - m0(w[k] / 2) * m0(w[k] / 4)) < 1e-15);
    }
    CHECK(spectrum_factors(8 * pi) >= 34);
}

TEST_CASE("scaling spectrum refuses a lowpass without a zero at -1", "[core][spectra]") {
    const Laurent no_zero{{1.0, 0.5}, 0};
    const auto r = scaling_spectrum(no_zero, std::vector<double>{1.0});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::invalid_argument);
    CHECK(r.error().message.find("H(-1)") != std::string::npos);
    const Laurent no_gain{{1.0, -1.0}, 0};
    const auto z = scaling_spectrum(no_gain, std::vector<double>{1.0});
    REQUIRE_FALSE(z);
    CHECK(z.error().message.find("H(1)") != std::string::npos);
}

TEST_CASE("continuous wavelet spectra match the numerical transform of psi", "[core][spectra]") {
    for (const char* name : {"morl", "mexh", "cmor1.5-1.0", "cmor0.5-2.0"}) {
        const auto cw = continuous_wavelet(name);
        REQUIRE(cw);
        const std::vector<double> w{0.0, 1.0, 3.0, 5.0, 6.283, 9.0, -4.0};
        const auto hat = continuous_spectrum(*cw, w);
        REQUIRE(hat.size() == w.size());
        constexpr int n = 40000;
        constexpr double lo = -12, hi = 12, dt = (hi - lo) / n;
        for (std::size_t k = 0; k < w.size(); ++k) {
            std::complex<double> sum{0, 0};
            for (int i = 0; i <= n; ++i) {
                const double t = lo + dt * i;
                std::complex<double> psi;
                switch (cw->family) {
                case ContinuousWavelet::Family::morl: psi = std::cos(5 * t) * std::exp(-t * t / 2); break;
                case ContinuousWavelet::Family::mexh: psi = 2 / (std::sqrt(3.0) * std::pow(pi, 0.25)) * (1 - t * t) * std::exp(-t * t / 2); break;
                case ContinuousWavelet::Family::cmor:
                    psi = std::exp(std::complex<double>{0, 2 * pi * cw->center * t}) * std::exp(-t * t / cw->bandwidth) / std::sqrt(pi * cw->bandwidth);
                    break;
                }
                sum += (i == 0 || i == n ? 0.5 : 1.0) * psi * std::polar(1.0, -w[k] * t) * dt;
            }
            CHECK(std::abs(hat[k] - sum) < 1e-9);
        }
    }
}
