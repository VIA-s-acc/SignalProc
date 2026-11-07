#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "core/metrics.hpp"
#include "core/pitch.hpp"
#include "core/tempo.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double gap(const std::vector<double>& a, const std::vector<double>& b) { return a.size() == b.size() ? max_abs_error(a, b) : 1e300; }

std::vector<double> tone(double f0, double fs, std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = std::sin(2 * std::numbers::pi * f0 * t) + 0.5 * std::sin(2 * std::numbers::pi * 2 * f0 * t + 0.4) +
               0.25 * std::sin(2 * std::numbers::pi * 3 * f0 * t + 1.3);
    }
    return x;
}

double cents(double f, double reference) { return 1200 * std::log2(f / reference); }

double pitch_of(const std::vector<double>& x, double fs) {
    const auto track = yin_track(x, fs, YinOptions{2048, 512, 60, 1000, 0.1});
    REQUIRE(track);
    return median_f0(*track);
}

std::vector<double> noise(std::size_t n, unsigned seed) {
    std::mt19937 rng{seed};
    std::normal_distribution<double> d;
    std::vector<double> x(n);
    for (auto& v : x) v = d(rng);
    return x;
}

}  // namespace

TEST_CASE("the phase vocoder matches librosa time_stretch", "[tempo][golden]") {
    const auto g = signals::test::load_golden("tempo.json");
    const auto x = g["x"].get<std::vector<double>>();
    for (const auto& c : g["stretch"]) {
        const double rate = c["rate"].get<double>();
        INFO("rate " << rate);
        const auto y = time_stretch(x, rate, {StretchMethod::vocoder, c["n_fft"].get<std::size_t>(), c["hop"].get<std::size_t>()});
        REQUIRE(y);
        CHECK(gap(*y, c["y"].get<std::vector<double>>()) < 1e-12);
    }
}

TEST_CASE("pitch shift matches librosa pitch_shift with the fft resampler", "[tempo][golden]") {
    const auto g = signals::test::load_golden("tempo.json");
    const auto x = g["x"].get<std::vector<double>>();
    for (const auto& c : g["shift"]) {
        INFO("semitones " << c["semitones"].get<double>());
        const auto y = pitch_shift(x, c["semitones"].get<double>(), {StretchMethod::vocoder, c["n_fft"].get<std::size_t>(), c["hop"].get<std::size_t>()});
        REQUIRE(y);
        CHECK(gap(*y, c["y"].get<std::vector<double>>()) < 1e-12);
    }
}

TEST_CASE("fft resampling matches scipy.signal.resample", "[tempo][golden]") {
    const auto g = signals::test::load_golden("tempo.json");
    for (const auto& c : g["resample"]) {
        const auto x = c["x"].get<std::vector<double>>();
        INFO(x.size() << " -> " << c["num"].get<std::size_t>());
        const auto y = fft_resample(x, c["num"].get<std::size_t>());
        REQUIRE(y);
        CHECK(gap(*y, c["y"].get<std::vector<double>>()) < 1e-12);
    }
}

TEST_CASE("every method keeps the length and leaves the input alone at rate 1", "[tempo]") {
    const auto method = GENERATE(StretchMethod::vocoder, StretchMethod::locked, StretchMethod::wsola);
    INFO(std::string{stretch_method_name(method)});
    const auto x = noise(6000, 7);
    const StretchOptions o{method, 512, 128};
    const auto same = time_stretch(x, 1.0, o);
    REQUIRE(same);
    CHECK(gap(*same, x) < 1e-9);
    for (const double rate : {0.1, 0.37, 0.8, 1.5, 3.0, 10.0}) {
        const auto y = time_stretch(x, rate, o);
        REQUIRE(y);
        CHECK(y->size() == static_cast<std::size_t>(std::nearbyint(6000 / rate)));
        for (const double v : *y) REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("stretching keeps the pitch and shifting by an octave moves it within a cent", "[tempo]") {
    const auto method = GENERATE(StretchMethod::vocoder, StretchMethod::locked, StretchMethod::wsola);
    INFO(std::string{stretch_method_name(method)});
    constexpr double fs = 44100;
    const auto x = tone(220, fs, 44100);
    const StretchOptions o{method, 2048, 512};
    for (const double rate : {0.75, 1.5}) {
        const auto y = time_stretch(x, rate, o);
        REQUIRE(y);
        CHECK(std::abs(cents(pitch_of(*y, fs), 220)) < 1);
    }
    const auto up = pitch_shift(x, 12, o);
    const auto down = pitch_shift(x, -12, o);
    REQUIRE(up);
    REQUIRE(down);
    CHECK(up->size() == x.size());
    CHECK(std::abs(cents(pitch_of(*up, fs), 440)) < 1);
    CHECK(std::abs(cents(pitch_of(*down, fs), 110)) < 1);
    const auto back = pitch_shift(*up, -12, o);
    REQUIRE(back);
    CHECK(std::abs(cents(pitch_of(*back, fs), 220)) < 1);
}

TEST_CASE("short, silent and constant inputs stay finite", "[tempo]") {
    const auto method = GENERATE(StretchMethod::vocoder, StretchMethod::locked, StretchMethod::wsola);
    const StretchOptions o{method, 512, 128};
    for (const auto& x : {std::vector<double>{0.5}, std::vector<double>(100, 0.0), std::vector<double>(3000, 0.25), std::vector<double>(127, 1.0)}) {
        for (const double rate : {0.5, 2.0}) {
            const auto y = time_stretch(x, rate, o);
            REQUIRE(y);
            CHECK(y->size() == static_cast<std::size_t>(std::nearbyint(static_cast<double>(x.size()) / rate)));
            for (const double v : *y) REQUIRE(std::isfinite(v));
        }
        const auto s = pitch_shift(x, 24, o);
        REQUIRE(s);
        CHECK(s->size() == x.size());
        for (const double v : *s) REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("bad stretch arguments give a reason", "[tempo]") {
    const auto x = noise(1000, 3);
    CHECK_FALSE(time_stretch({}, 1.0, {}));
    CHECK_FALSE(time_stretch(x, 0.0, {}));
    CHECK_FALSE(time_stretch(x, 0.05, {}));
    CHECK_FALSE(time_stretch(x, 11.0, {}));
    CHECK_FALSE(time_stretch(x, std::nan(""), {}));
    CHECK_FALSE(time_stretch(x, 1.2, {StretchMethod::vocoder, 511, 128}));
    CHECK_FALSE(time_stretch(x, 1.2, {StretchMethod::vocoder, 8, 4}));
    CHECK_FALSE(time_stretch(x, 1.2, {StretchMethod::vocoder, 512, 0}));
    CHECK_FALSE(time_stretch(x, 1.2, {StretchMethod::vocoder, 512, 513}));
    CHECK_FALSE(pitch_shift(x, 25, {}));
    CHECK_FALSE(pitch_shift(x, std::nan(""), {}));
    CHECK_FALSE(fft_resample({}, 4));
    CHECK_FALSE(fft_resample(x, 0));
    CHECK(parse_stretch_method("wsola") == StretchMethod::wsola);
    CHECK_FALSE(parse_stretch_method("granular"));
}
