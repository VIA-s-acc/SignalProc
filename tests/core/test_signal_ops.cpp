#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/signal_ops.hpp"
#include "core/wavedec.hpp"
#include "support/banks.hpp"

using namespace signals::core;

namespace {

double peak(const std::vector<double>& x) {
    double p = 0;
    for (double v : x) p = std::max(p, std::abs(v));
    return p;
}

std::vector<double> uniform_noise(std::size_t n, unsigned seed) {
    std::mt19937_64 rng{seed};
    std::uniform_real_distribution<double> u{-1.0, 1.0};
    std::vector<double> x(n);
    for (double& v : x) v = u(rng);
    return x;
}

}  // namespace

TEST_CASE("mid side round trip is exact up to rounding and keeps energy", "[ops]") {
    AudioBuffer a{48'000, {uniform_noise(1000, 1), uniform_noise(1000, 2)}};
    auto ms = to_mid_side(a);
    REQUIRE(ms);
    auto back = from_mid_side(*ms);
    REQUIRE(back);
    CHECK(back->sample_rate == 48'000);
    for (std::size_t c = 0; c < 2; ++c) CHECK(max_abs_error(back->channels[c], a.channels[c]) <= 4e-16);
    const double before = energy(a.channels[0]) + energy(a.channels[1]);
    const double after = energy(ms->channels[0]) + energy(ms->channels[1]);
    CHECK(std::abs(after - before) <= 1e-12 * before);
    CHECK(to_mid_side(AudioBuffer{48'000, {{1.0}}}).error().code == Errc::invalid_argument);
    CHECK(from_mid_side(AudioBuffer{48'000, {{1.0}, {1.0}, {1.0}}}).error().code == Errc::invalid_argument);
}

TEST_CASE("normalisation hits the target and leaves silence alone", "[ops]") {
    auto x = uniform_noise(4096, 3);
    CHECK(20 * std::log10(peak(normalize_peak(x, -3))) == Catch::Approx(-3).margin(1e-12));
    auto y = normalize_rms(x, -20);
    CHECK(10 * std::log10(energy(y) / y.size()) == Catch::Approx(-20).margin(1e-12));
    const std::vector<double> zeros(16, 0.0);
    CHECK(normalize_peak(zeros, -3) == zeros);
    CHECK(normalize_rms(zeros, -3) == zeros);
    CHECK(gain_db(std::vector<double>{1.0, -0.5}, 20 * std::log10(2.0)) == std::vector<double>{2.0, -1.0});
}

TEST_CASE("dc removal, inversion and channel selection", "[ops]") {
    CHECK(remove_dc(std::vector<double>{1, 2, 3}) == std::vector<double>{-1, 0, 1});
    CHECK(invert(std::vector<double>{1, -2}) == std::vector<double>{-1, 2});
    AudioBuffer a{8000, {{1, 3}, {3, 5}}};
    auto m = mixdown(a);
    CHECK(m.sample_rate == 8000);
    REQUIRE(m.channels.size() == 1);
    CHECK(m.channels[0] == std::vector<double>{2, 4});
    CHECK(select_channel(a, 1)->channels == std::vector<std::vector<double>>{{3, 5}});
    CHECK(select_channel(a, 2).error().code == Errc::invalid_argument);
}

TEST_CASE("time operations keep indices straight", "[ops]") {
    CHECK(delay(std::vector<double>{1, 2}, 2) == std::vector<double>{0, 0, 1, 2});
    CHECK(reverse(std::vector<double>{1, 2, 3}) == std::vector<double>{3, 2, 1});
    const std::vector<double> x{1, 2, 3, 4};
    CHECK(cut(x, 2, 1).error().code == Errc::invalid_argument);
    CHECK(cut(x, 1, 5).error().code == Errc::invalid_argument);
    CHECK(*cut(x, 1, 3) == std::vector<double>{2, 3});
    CHECK(cut(x, 4, 4)->empty());
}

TEST_CASE("hard clip counts", "[ops]") {
    auto r = hard_clip(std::vector<double>{-2, 0.5, 3}, 1);
    CHECK(r.samples == std::vector<double>{-1, 0.5, 1});
    CHECK(r.clipped == 2);
}

TEST_CASE("psnr and band energy fractions", "[ops]") {
    CHECK(psnr_db(std::vector<double>{1, 0, 0, 0}, std::vector<double>{1, 0, 0, 0.1}, 1) ==
          Catch::Approx(10 * std::log10(1 / (0.01 / 4))).margin(1e-12));
    CHECK(psnr_db(std::vector<double>{1, 2}, std::vector<double>{1, 2}, 1) == std::numeric_limits<double>::infinity());
    const auto x = signals::test::signal_of_length(1024);
    auto c = wavedec<double>(x, *wavelet("db2"), 6, {Extension::periodization});
    REQUIRE(c);
    auto f = band_energy_fractions(*c);
    REQUIRE(f.size() == 7);
    CHECK(std::abs(neumaier_sum(f) - 1.0) <= 1e-14);
    double bands = 0;
    for (const auto& b : c->bands) bands += energy(b);
    CHECK(std::abs(bands - energy(x)) <= 1e-12 * energy(x));
    BasicCoeffs<double> zero{{{0.0, 0.0}, {0.0, 0.0}}, 4};
    CHECK(band_energy_fractions(zero) == std::vector<double>{0.0, 0.0});
}
