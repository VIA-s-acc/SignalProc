#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/metrics.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double gap(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return 1e300;
    return max_abs_error(a, b);
}

}  // namespace

TEST_CASE("correlate matches scipy in every mode", "[analysis][golden]") {
    const auto g = signals::test::load_golden("analysis.json");
    for (const auto& c : g["correlate"]) {
        const auto mode = c["mode"].get<std::string>();
        INFO(mode);
        const auto m = mode == "full" ? CorrelationMode::full : mode == "same" ? CorrelationMode::same : CorrelationMode::valid;
        const auto out = correlate(c["x"].get<std::vector<double>>(), c["y"].get<std::vector<double>>(), m);
        REQUIRE(out);
        CHECK(gap(*out, c["out"].get<std::vector<double>>()) < 1e-12);
    }
    CHECK_FALSE(correlate(std::vector<double>{}, std::vector<double>{1.0}, CorrelationMode::full));
}

TEST_CASE("long correlations take the fft path and agree with the direct sum", "[analysis]") {
    std::vector<double> x(3000), y(700);
    for (std::size_t n = 0; n < x.size(); ++n) x[n] = std::sin(0.01 * static_cast<double>(n * n % 977));
    for (std::size_t n = 0; n < y.size(); ++n) y[n] = std::cos(0.37 * static_cast<double>(n));
    const auto fast = correlate(x, y, CorrelationMode::full);
    REQUIRE(fast);
    CHECK(fast->size() == x.size() + y.size() - 1);
    double worst = 0;
    for (std::size_t k = 0; k < fast->size(); k += 97) {
        double s = 0;
        const auto lag = static_cast<std::ptrdiff_t>(k) - static_cast<std::ptrdiff_t>(y.size()) + 1;
        for (std::size_t m = 0; m < y.size(); ++m) {
            const auto i = lag + static_cast<std::ptrdiff_t>(m);
            if (i >= 0 && i < static_cast<std::ptrdiff_t>(x.size())) s += x[static_cast<std::size_t>(i)] * y[m];
        }
        worst = std::max(worst, std::abs(s - (*fast)[k]));
    }
    CHECK(worst < 1e-9);
}

TEST_CASE("the analytic signal, cepstrum and dct match scipy", "[analysis][golden]") {
    const auto g = signals::test::load_golden("analysis.json");
    for (const auto& c : g["hilbert"]) {
        const auto a = analytic_signal(c["x"].get<std::vector<double>>());
        std::vector<double> re, im;
        for (const auto& v : a) re.push_back(v.real()), im.push_back(v.imag());
        CHECK(gap(re, c["re"].get<std::vector<double>>()) < 1e-12);
        CHECK(gap(im, c["im"].get<std::vector<double>>()) < 1e-12);
    }
    for (const auto& c : g["cepstrum"]) CHECK(gap(real_cepstrum(c["x"].get<std::vector<double>>()), c["out"].get<std::vector<double>>()) < 1e-9);
    for (const auto& c : g["dct"]) {
        const auto x = c["x"].get<std::vector<double>>();
        const auto d = dct2(x);
        CHECK(gap(d, c["dct"].get<std::vector<double>>()) < 1e-12);
        CHECK(gap(idct2(d), x) < 1e-12);
    }
}

TEST_CASE("envelope and instantaneous frequency of a tone", "[analysis]") {
    constexpr double fs = 48000, f = 1000;
    std::vector<double> x(4800);
    for (std::size_t n = 0; n < x.size(); ++n) x[n] = 0.5 * std::sin(2 * std::numbers::pi * f * static_cast<double>(n) / fs);
    const auto env = envelope(x);
    const auto inst = instantaneous_frequency(x, fs);
    REQUIRE(inst.size() == x.size() - 1);
    for (std::size_t n = 480; n + 480 < x.size(); ++n) {
        CHECK(std::abs(env[n] - 0.5) < 1e-3);
        CHECK(std::abs(inst[n] - f) < 1e-3);
    }
    std::vector<double> gapped(x);
    for (std::size_t n = 2000; n < 2400; ++n) gapped[n] = 0;
    for (double v : instantaneous_frequency(gapped, fs)) CHECK(std::isfinite(v));
}

TEST_CASE("stft and istft match scipy and reconstruct", "[analysis][golden]") {
    const auto g = signals::test::load_golden("analysis.json");
    const auto& s = g["stft"];
    const auto x = s["x"].get<std::vector<double>>();
    const std::size_t n = s["n"], hop = s["hop"];
    const auto z = stft_complex(x, n, hop);
    REQUIRE(z);
    REQUIRE(z->frames.size() == s["re"].size());
    double worst = 0;
    for (std::size_t m = 0; m < z->frames.size(); ++m)
        for (std::size_t k = 0; k < z->frames[m].size(); ++k)
            worst = std::max(worst, std::abs(z->frames[m][k] - std::complex<double>{s["re"][m][k].get<double>(), s["im"][m][k].get<double>()}));
    CHECK(worst < 1e-12);
    const auto back = istft(*z);
    REQUIRE(back);
    CHECK(gap(*back, s["istft"].get<std::vector<double>>()) < 1e-12);
    CHECK_FALSE(stft_complex(x, 16, 0));
    CHECK_FALSE(stft_complex(x, 16, 17));
}

TEST_CASE("thd of a tone with known harmonics", "[analysis]") {
    constexpr double fs = 48000;
    std::vector<double> x(48000);
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double t = static_cast<double>(n) / fs;
        x[n] = std::sin(2 * std::numbers::pi * 1000 * t) + 0.01 * std::sin(2 * std::numbers::pi * 2000 * t) + 0.001 * std::sin(2 * std::numbers::pi * 3000 * t);
    }
    const auto r = thd(x, fs, 5);
    REQUIRE(r);
    CHECK(std::abs(r->f0 - 1000) < 0.01);
    CHECK(std::abs(r->thd / std::hypot(0.01, 0.001) - 1) < 1e-3);
    REQUIRE(r->harmonic_db.size() == 4);
    CHECK(std::abs(r->harmonic_db[0] + 40) < 0.05);
    CHECK(std::abs(r->harmonic_db[1] + 60) < 0.05);
    std::vector<double> off(30011);
    for (std::size_t n = 0; n < off.size(); ++n) off[n] = std::sin(2 * std::numbers::pi * 441.7 * static_cast<double>(n) / 44100) + 0.02 * std::sin(2 * std::numbers::pi * 883.4 * static_cast<double>(n) / 44100);
    const auto o = thd(off, 44100, 3);
    REQUIRE(o);
    CHECK(std::abs(o->f0 - 441.7) < 0.1);
    CHECK(std::abs(o->thd / 0.02 - 1) < 0.02);
    CHECK_FALSE(thd(std::vector<double>(100, 0.0), fs, 5));
}

TEST_CASE("onsets find synthetic clicks within two hops", "[analysis]") {
    constexpr double fs = 16000;
    std::vector<double> x(32000, 0.0);
    const std::vector<double> clicks{0.25, 0.75, 1.3, 1.6};
    std::uint32_t state = 1;
    for (double t0 : clicks)
        for (std::size_t k = 0; k < 160; ++k) {
            state = state * 1664525u + 1013904223u;
            x[static_cast<std::size_t>(t0 * fs) + k] = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * std::exp(-static_cast<double>(k) / 40);
        }
    const auto found = onsets(x, fs, 1024, 256);
    REQUIRE(found.size() == clicks.size());
    for (std::size_t i = 0; i < clicks.size(); ++i) CHECK(std::abs(found[i] - clicks[i]) <= 2 * 256 / fs);
    CHECK(onsets(std::vector<double>(32000, 0.0), fs, 1024, 256).empty());
}
