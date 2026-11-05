#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "core/detail/pocketfft.hpp"
#include "core/metrics.hpp"
#include "io/generators.hpp"
#include "support/golden.hpp"

using namespace signals::io;
using signals::core::energy;
using signals::core::max_abs_error;

namespace {

double log_log_power_slope(const std::vector<double>& x, std::size_t k_lo, std::size_t k_hi) {
    std::vector<std::complex<double>> spectrum(x.size() / 2 + 1);
    pocketfft::r2c({x.size()}, {sizeof(double)}, {sizeof(std::complex<double>)}, 0, pocketfft::FORWARD, x.data(),
                   spectrum.data(), 1.0);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto m = static_cast<double>(k_hi - k_lo);
    for (std::size_t k = k_lo; k < k_hi; ++k) {
        const double lx = std::log10(static_cast<double>(k)), ly = std::log10(std::norm(spectrum[k]));
        sx += lx;
        sy += ly;
        sxx += lx * lx;
        sxy += lx * ly;
    }
    return (m * sxy - sx * sy) / (m * sxx - sx * sx);
}

DonohoJohnstone dj_kind(const std::string& name) {
    if (name == "Blocks") return DonohoJohnstone::blocks;
    if (name == "Bumps") return DonohoJohnstone::bumps;
    if (name == "HeaviSine") return DonohoJohnstone::heavisine;
    return DonohoJohnstone::doppler;
}

}  // namespace

TEST_CASE("sweeps match scipy chirp", "[generators][golden]") {
    const auto golden = signals::test::load_golden("signals.json");
    for (auto& c : golden["chirp"]) {
        INFO(c["method"]);
        const auto kind = c["method"] == "linear" ? SweepKind::linear : SweepKind::logarithmic;
        auto y = sweep(c["f0"], c["f1"], kind, c["fs"], c["n"].get<std::size_t>());
        CHECK(max_abs_error(y, c["y"].get<std::vector<double>>()) <= 1e-9);
    }
}

TEST_CASE("donoho johnstone signals match pywt", "[generators][golden]") {
    const auto golden = signals::test::load_golden("signals.json");
    for (auto& c : golden["dj"]) {
        INFO(c["name"] << " n=" << c["n"]);
        auto y = donoho_johnstone(dj_kind(c["name"]), c["n"].get<std::size_t>());
        CHECK(max_abs_error(y, c["y"].get<std::vector<double>>()) <= 1e-12);
    }
}

TEST_CASE("noise colours have the right spectral slope", "[generators]") {
    for (auto [color, slope] : {std::pair{NoiseColor::white, 0.0}, std::pair{NoiseColor::pink, -1.0},
                                std::pair{NoiseColor::brown, -2.0}}) {
        auto x = noise(color, 0.1, 1 << 16, 42);
        CHECK(std::sqrt(energy(x) / x.size()) == Catch::Approx(0.1).epsilon(1e-12));
        CHECK(log_log_power_slope(x, 16, 1 << 14) == Catch::Approx(slope).margin(0.15));
    }
    CHECK(noise(NoiseColor::pink, 0.1, 1024, 7) == noise(NoiseColor::pink, 0.1, 1024, 7));
    CHECK(noise(NoiseColor::white, 0.1, 1024, 7) != noise(NoiseColor::white, 0.1, 1024, 8));
    CHECK(noise(NoiseColor::brown, 0.1, 0, 7).empty());
}

TEST_CASE("impulse, step, am and fm are exact", "[generators]") {
    CHECK(impulse(8, 3) == std::vector<double>{0, 0, 0, 1, 0, 0, 0, 0});
    CHECK(impulse(3, 5) == std::vector<double>(3, 0.0));
    CHECK(step(5, 2) == std::vector<double>{0, 0, 1, 1, 1});
    const std::size_t n = 480;
    auto a = am(1000, 10, 0.5, 48'000, n);
    auto f = fm(1000, 10, 2.0, 48'000, n);
    const auto w = [](double freq, std::size_t k) { return 2 * std::numbers::pi * freq * static_cast<double>(k) / 48'000; };
    for (std::size_t k = 0; k < n; ++k) {
        CHECK(std::abs(a[k] - (1 + 0.5 * std::sin(w(10, k))) * std::sin(w(1000, k))) <= 1e-15);
        CHECK(std::abs(f[k] - std::sin(w(1000, k) + 2.0 * std::sin(w(10, k)))) <= 1e-15);
    }
    const std::vector<double> amps{1, 0.5};
    auto h = harmonics(100, amps, 8000, n);
    auto s1 = sine(100, 1, 0, 8000, n), s2 = sine(200, 0.5, 0, 8000, n);
    for (std::size_t k = 0; k < n; ++k) CHECK(std::abs(h[k] - (s1[k] + s2[k])) <= 1e-15);
    CHECK(sine(1, 2.0, std::numbers::pi / 2, 4, 1)[0] == 2.0);
}

TEST_CASE("degenerate sweeps are constant", "[generators]") {
    CHECK(sweep(100, 200, SweepKind::linear, 8000, 1) == std::vector<double>{1.0});
    CHECK(sweep(100, 200, SweepKind::logarithmic, 8000, 0).empty());
}
