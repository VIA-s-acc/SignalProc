#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/metrics.hpp"
#include "core/restoration.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double gap(const std::vector<double>& a, const std::vector<double>& b) { return a.size() == b.size() ? max_abs_error(a, b) : 1e300; }

}  // namespace

TEST_CASE("the local wiener filter matches scipy.signal.wiener", "[restoration][golden]") {
    const auto g = signals::test::load_golden("restoration.json");
    const auto x = g["x"].get<std::vector<double>>();
    for (const auto& c : g["wiener"]) {
        INFO("size " << c["size"].get<int>());
        const std::optional<double> noise = c["noise"].is_null() ? std::nullopt : std::optional<double>{c["noise"].get<double>()};
        const auto out = wiener_local(x, c["size"].get<std::size_t>(), noise);
        REQUIRE(out);
        CHECK(gap(*out, c["out"].get<std::vector<double>>()) < 1e-12);
    }
    CHECK_FALSE(wiener_local(x, 4));
    CHECK_FALSE(wiener_local(x, 0));
    const auto flat = wiener_local(std::vector<double>(50, 0.5), 5);
    REQUIRE(flat);
    for (const double v : *flat) CHECK(std::isfinite(v));
}

TEST_CASE("spectral subtraction matches the numpy and scipy reference", "[restoration][golden]") {
    const auto g = signals::test::load_golden("restoration.json");
    const auto x = g["x"].get<std::vector<double>>();
    const auto noise = g["noise"].get<std::vector<double>>();
    for (const auto& c : g["subtract"]) {
        SpectralSubtraction o;
        o.alpha = c["alpha"];
        o.beta = c["beta"];
        o.n = c["n"];
        o.hop = c["hop"];
        INFO("n " << o.n << " hop " << o.hop);
        const auto out = spectral_subtract(x, noise, o);
        REQUIRE(out);
        CHECK(gap(*out, c["out"].get<std::vector<double>>()) < 1e-12);
    }
    SpectralSubtraction o;
    o.n = 128;
    o.hop = 32;
    const auto short_noise = spectral_subtract(x, std::vector<double>(64, 0.1), o);
    REQUIRE_FALSE(short_noise);
    CHECK(short_noise.error().message.find("noise") != std::string::npos);
    CHECK_FALSE(spectral_subtract(std::vector<double>(64, 0.1), noise, o));
}

TEST_CASE("wiener deconvolution matches numpy and refuses bad kernels", "[restoration][golden]") {
    const auto g = signals::test::load_golden("restoration.json");
    const auto y = g["blurred"].get<std::vector<double>>();
    const auto h = g["h"].get<std::vector<double>>();
    for (const auto& c : g["deconvolve"]) {
        INFO("snr " << c["snr_db"].get<double>());
        const auto out = wiener_deconvolve(y, h, c["snr_db"]);
        REQUIRE(out);
        CHECK(gap(*out, c["out"].get<std::vector<double>>()) < 1e-12);
    }
    const auto sharp = wiener_deconvolve(y, h, 120);
    REQUIRE(sharp);
    CHECK(gap(*sharp, g["clean"].get<std::vector<double>>()) < 1e-4);
    CHECK_FALSE(wiener_deconvolve(y, {}, 30));
    CHECK_FALSE(wiener_deconvolve(std::vector<double>(3, 1.0), h, 30));
    const std::vector<double> notch{0.5, 0.5};
    const auto stable = wiener_deconvolve(y, notch, 30);
    REQUIRE(stable);
    for (const double v : *stable) CHECK(std::isfinite(v));
}
