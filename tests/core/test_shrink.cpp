#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/check.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/process/shrink.hpp"
#include "io/wav.hpp"
#include "support/golden.hpp"
#include "support/paths.hpp"

using namespace signals::core;

namespace {

std::vector<double> music_excerpt() {
    auto w = signals::io::read_wav(signals::test::sample("test.wav"));
    REQUIRE(w);
    const auto& left = w->audio.channels[0];
    return {left.begin() + 100'000, left.begin() + 165'536};
}

std::vector<double> add_noise_at_snr(const std::vector<double>& clean, double snr_db_in, std::uint64_t seed) {
    const double s = std::sqrt(energy(clean) / static_cast<double>(clean.size()) / std::pow(10.0, snr_db_in / 10));
    std::mt19937_64 rng{seed};
    std::normal_distribution<double> normal{0.0, s};
    auto noisy = clean;
    for (double& v : noisy) v += normal(rng);
    return noisy;
}

double gain_db(const std::vector<double>& clean, const std::vector<double>& noisy, const DenoiseOptions& o) {
    auto r = denoise(noisy, o);
    REQUIRE(r);
    return snr_db(clean, r->first) - snr_db(clean, noisy);
}

}  // namespace

TEST_CASE("noise estimate and thresholds follow scikit-image", "[shrink]") {
    CHECK(noise_sigma_mad(std::vector<double>{0.0, 1.0, -2.0, 0.0, 3.0}) == 2.0 / kNormalQuartile);
    CHECK(noise_sigma_mad(std::vector<double>{1.0, -2.0, 3.0, 4.0}) == 2.5 / kNormalQuartile);
    CHECK(noise_sigma_mad(std::vector<double>(8, 0.0)) == 0.0);
    CHECK(universal_threshold(2.0, 100) == 2.0 * std::sqrt(2 * std::log(100.0)));
    CHECK(bayes_threshold(std::vector<double>{1.0, -1.0}, 0.0) == 0.0);
    CHECK(bayes_threshold(std::vector<double>{3.0, -3.0}, 1.0) == Catch::Approx(1.0 / std::sqrt(8.0)).epsilon(1e-15));
}

TEST_CASE("denoise matches scikit-image", "[shrink][golden]") {
    const auto golden = signals::test::load_golden("denoise.json");
    for (auto& c : golden["cases"]) {
        INFO(c["signal"] << " " << c["wavelet"] << " " << c["method"] << " " << c["mode"] << " levels=" << c["levels"]);
        DenoiseOptions o{c["wavelet"],
                         c["levels"].is_null() ? std::nullopt : std::optional<std::size_t>{c["levels"].get<std::size_t>()},
                         Extension::symmetric,
                         c["method"] == "BayesShrink" ? ShrinkRule::bayes : ShrinkRule::visu,
                         *parse_threshold_mode(c["mode"].get<std::string>()),
                         std::nullopt};
        const auto noisy = golden["signals"][c["signal"].get<std::string>()]["noisy"].get<std::vector<double>>();
        auto r = denoise(noisy, o);
        REQUIRE(r);
        double peak = 0;
        for (double v : noisy) peak = std::max(peak, std::abs(v));
        CHECK(r->second.sigma == Catch::Approx(c["sigma"].get<double>()).epsilon(5e-11));
        CHECK(max_abs_error(r->first, c["out"].get<std::vector<double>>()) <= 2e-11 * peak);
    }
}

TEST_CASE("bayes shrink improves noisy music by at least 4 dB", "[shrink]") {
    const auto clean = music_excerpt();
    const auto noisy = add_noise_at_snr(clean, 10.0, 7);
    DenoiseOptions bayes;
    bayes.levels = 5;
    DenoiseOptions visu = bayes;
    visu.rule = ShrinkRule::visu;
    const double bayes_gain = gain_db(clean, noisy, bayes);
    const double visu_gain = gain_db(clean, noisy, visu);
    CHECK(bayes_gain >= 4.0);
    CHECK(visu_gain < bayes_gain);
}

TEST_CASE("bayes shrink improves doppler by at least 6 dB", "[shrink]") {
    const auto golden = signals::test::load_golden("denoise.json");
    const auto d = golden["signals"]["doppler"];
    const auto clean = d["clean"].get<std::vector<double>>(), noisy = d["noisy"].get<std::vector<double>>();
    DenoiseOptions o;
    o.levels = 5;
    const double g = gain_db(clean, noisy, o);
    CHECK(g >= 6.0);
}

TEST_CASE("silence passes through denoise unchanged", "[shrink]") {
    std::vector<double> z(4096, 0.0);
    auto r = denoise(z, {});
    REQUIRE(r);
    CHECK(r->first == z);
    CHECK(r->second.sigma == 0.0);
}

TEST_CASE("stereo channels are denoised independently", "[shrink]") {
    const auto clean = music_excerpt();
    AudioBuffer a{44'100, {add_noise_at_snr(clean, 10.0, 7), std::vector<double>(clean.size(), 0.0)}};
    auto r = denoise(a, {});
    REQUIRE(r);
    REQUIRE(r->first.channels.size() == 2);
    CHECK(r->first.channels[1] == a.channels[1]);
    REQUIRE(r->second.size() == 2);
    CHECK(r->second[0].sigma > 0);
    CHECK(r->second[1].sigma == 0.0);
    CHECK(r->first.sample_rate == 44'100);
}

TEST_CASE("short signals and unknown wavelets are errors", "[shrink]") {
    CHECK(denoise(std::vector<double>(10, 1.0), {}).error().code == Errc::level_out_of_range);
    DenoiseOptions o;
    o.wavelet = "nope";
    CHECK(denoise(std::vector<double>(64, 1.0), o).error().code == Errc::unknown_wavelet);
}

TEST_CASE("unit band gains reproduce the signal", "[shrink]") {
    const auto x = music_excerpt();
    const auto bank = *wavelet("db4");
    const std::vector<double> ones(5, 1.0);
    auto y = edit_bands(x, bank, 4, {}, ones);
    REQUIRE(y);
    double peak = 0;
    for (double v : x) peak = std::max(peak, std::abs(v));
    CHECK(max_abs_error(*y, x) <= default_tolerance(4, bank.length(), peak));
    CHECK(edit_bands(x, bank, 4, {}, std::vector<double>(4, 1.0)).error().code == Errc::invalid_argument);
    const std::vector<double> mute_details{1, 0, 0, 0, 0};
    auto low = edit_bands(x, bank, 4, {}, mute_details);
    REQUIRE(low);
    CHECK(energy(*low) < energy(x));
}

TEST_CASE("keep_largest leaves the requested share of details", "[core][shrink]") {
    std::vector<std::vector<double>> b{{9, 9}, {1, -5, 3}, {0.5, -4, 2, 7, 0.1, 6, 0.2}};
    CHECK(keep_largest(b, 0.3) == 3);
    CHECK(b[0] == std::vector<double>{9, 9});
    CHECK(b[1] == std::vector<double>{0, -5, 0});
    CHECK(b[2] == std::vector<double>{0, 0, 0, 7, 0, 6, 0});
}

TEST_CASE("keep_largest keeps everything at one and earlier ties first", "[core][shrink]") {
    std::vector<std::vector<double>> b{{1}, {2, -2, 2, 1}};
    CHECK(keep_largest(b, 1.0) == 4);
    CHECK(b[1] == std::vector<double>{2, -2, 2, 1});
    CHECK(keep_largest(b, 0.5) == 2);
    CHECK(b[1] == std::vector<double>{2, -2, 0, 0});
    std::vector<std::vector<double>> none{{1}};
    CHECK(keep_largest(none, 0.1) == 0);
}

TEST_CASE("cycle spinning matches skimage", "[shrink][golden]") {
    const auto g = signals::test::load_golden("ops.json");
    const auto& c = g["cycle_spin"];
    const auto noisy = c["noisy"].get<std::vector<double>>();
    DenoiseOptions o;
    o.wavelet = "db4";
    o.levels = 5;
    o.rule = ShrinkRule::bayes;
    o.threshold = ThresholdMode::soft;
    const auto out = cycle_spin(noisy, c["shifts"].get<std::size_t>(), [&](std::span<const double> v) -> Result<std::vector<double>> {
        auto d = denoise(v, o);
        if (!d) return std::unexpected{d.error()};
        return std::move(d->first);
    });
    REQUIRE(out);
    CHECK(max_abs_error(*out, c["out"].get<std::vector<double>>()) < 1e-12);
    CHECK_FALSE(cycle_spin(noisy, 0, [](std::span<const double> v) -> Result<std::vector<double>> { return std::vector<double>(v.begin(), v.end()); }));
}
