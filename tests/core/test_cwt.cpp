#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "core/cwt.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

struct StopAfterFirst : ProgressSink {
    int reports = 0;
    void report(double) override { ++reports; }
    bool cancelled() const override { return reports > 0; }
};

}  // namespace

TEST_CASE("cwt matches pywt", "[cwt][golden]") {
    auto g = signals::test::load_golden("cwt.json");
    REQUIRE(g["cases"].size() > 0);
    for (auto& c : g["cases"]) {
        const auto n = c["n"].get<std::size_t>();
        const auto x = g["signals"][std::to_string(n)].get<std::vector<double>>();
        const auto name = c["wavelet"].get<std::string>();
        INFO(name << " n=" << n);
        const auto w = continuous_wavelet(name);
        REQUIRE(w);
        CHECK(center_frequency(*w) == Catch::Approx(c["center"].get<double>()).epsilon(1e-12));
        const auto scales = c["scales"].get<std::vector<double>>();
        const auto r = cwt(x, scales, *w);
        REQUIRE(r);
        REQUIRE(r->size() == scales.size());
        for (std::size_t i = 0; i < scales.size(); ++i) {
            INFO("scale " << scales[i]);
            const auto re = c["re"][i].get<std::vector<double>>();
            const auto im = c["im"][i].get<std::vector<double>>();
            REQUIRE((*r)[i].size() == n);
            double peak = 0, worst = 0;
            for (std::size_t k = 0; k < n; ++k) {
                peak = std::max(peak, std::abs(std::complex<double>{re[k], im[k]}));
                worst = std::max(worst, std::abs((*r)[i][k] - std::complex<double>{re[k], im[k]}));
            }
            CHECK(worst <= 1e-9 * std::max(peak, 1e-12));
            CHECK(c["frequencies"][i].get<double>() == Catch::Approx(center_frequency(*w) / scales[i]).epsilon(1e-12));
        }
    }
}

TEST_CASE("cwt rows arrive in scale order", "[cwt]") {
    std::vector<double> x(300);
    for (std::size_t k = 0; k < x.size(); ++k) x[k] = std::sin(0.2 * static_cast<double>(k));
    const std::vector<double> scales{1, 2, 4, 8};
    std::vector<std::size_t> seen;
    const auto ok = cwt_rows(x, scales, *continuous_wavelet("morl"), [&](std::size_t i, std::span<const std::complex<double>> row) {
        seen.push_back(i);
        CHECK(row.size() == x.size());
    });
    REQUIRE(ok);
    CHECK(seen == std::vector<std::size_t>{0, 1, 2, 3});
}

TEST_CASE("cwt refuses a scale too small and unknown wavelets", "[cwt]") {
    const std::vector<double> x(64, 1.0);
    const auto w = continuous_wavelet("morl");
    REQUIRE(w);
    const std::vector<double> tiny{0.05};
    const auto r = cwt(x, tiny, *w);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::invalid_argument);
    CHECK_FALSE(continuous_wavelet("db2"));
    CHECK_FALSE(continuous_wavelet("cmor"));
    CHECK_FALSE(continuous_wavelet("cmor0-1"));
    const auto cm = continuous_wavelet("cmor2.5-0.5");
    REQUIRE(cm);
    CHECK(cm->complex());
    CHECK(cm->bandwidth == 2.5);
    CHECK(cm->center == 0.5);
    CHECK_FALSE(continuous_wavelet("mexh")->complex());
}

TEST_CASE("cwt stops between scales when cancelled", "[cwt]") {
    const std::vector<double> x(256, 0.5);
    const std::vector<double> scales{1, 2, 3, 4, 5};
    StopAfterFirst stop;
    std::size_t rows = 0;
    const auto r = cwt_rows(x, scales, *continuous_wavelet("morl"), [&](std::size_t, std::span<const std::complex<double>>) { ++rows; }, &stop);
    REQUIRE_FALSE(r);
    CHECK(rows < scales.size());
}

TEST_CASE("the morlet scalogram peaks at the tone frequency", "[cwt]") {
    const double fs = 8000, f0 = 1000;
    std::vector<double> x(4096);
    for (std::size_t k = 0; k < x.size(); ++k) x[k] = std::sin(2 * std::numbers::pi * f0 * static_cast<double>(k) / fs);
    const auto w = *continuous_wavelet("morl");
    std::vector<double> freqs{250, 500, 1000, 2000, 3000};
    std::vector<double> scales;
    for (const double f : freqs) scales.push_back(center_frequency(w) * fs / f);
    std::vector<double> power(freqs.size(), 0.0);
    REQUIRE(cwt_rows(x, scales, w, [&](std::size_t i, std::span<const std::complex<double>> row) {
        for (std::size_t k = 1000; k < 3000; ++k) power[i] += std::norm(row[k]);
    }));
    CHECK(std::ranges::max_element(power) - power.begin() == 2);
}
