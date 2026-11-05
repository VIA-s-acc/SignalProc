#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <span>
#include <vector>

#include "core/metrics.hpp"
#include "core/multirate.hpp"
#include "io/generators.hpp"
#include "support/golden.hpp"
#include "support/spectrum.hpp"

using namespace signals::core;
using signals::test::dominant_frequency;
using signals::test::rms_db;

TEST_CASE("upfirdn matches scipy", "[multirate][golden]") {
    const auto golden = signals::test::load_golden("upfirdn.json");
    for (auto& c : golden["cases"]) {
        INFO(c["filter"] << " up=" << c["up"] << " down=" << c["down"]);
        const Laurent h{c["h"].get<std::vector<double>>(), 0};
        auto y = upfirdn(h, c["x"].get<std::vector<double>>(), c["up"], c["down"]);
        const auto expected = c["y"].get<std::vector<double>>();
        REQUIRE(y.size() == expected.size());
        CHECK(max_abs_error(y, expected) <= 1e-13);
    }
}

TEST_CASE("decimation keeps an in-band tone", "[multirate]") {
    auto x = signals::io::sine(1000, 0.5, 0, 48'000, 48'000);
    auto y = *decimate(x, 4, {});
    REQUIRE(y.size() == 12'000);
    const std::span<const double> mid{y.data() + 1000, 10'000};
    CHECK(std::sqrt(2 * energy(mid) / static_cast<double>(mid.size())) == Catch::Approx(0.5).margin(0.001));
    double phase_error = 0;
    for (std::size_t k = 1000; k < 11'000; ++k) phase_error = std::max(phase_error, std::abs(y[k] - x[4 * k]));
    CHECK(phase_error <= 2e-3);
    CHECK(decimate(std::vector<double>(7, 1.0), 4, {})->size() == 2);
}

TEST_CASE("without the anti-alias filter a tone folds", "[multirate]") {
    auto x = signals::io::sine(10'000, 0.5, 0, 48'000, 48'000);
    auto raw = *decimate(x, 4, {false});
    CHECK(dominant_frequency(raw, 12'000) == Catch::Approx(2'000).margin(2));
    auto filtered = *decimate(x, 4, {});
    const auto interior = [](const std::vector<double>& v) { return std::vector<double>(v.begin() + 200, v.end() - 200); };
    CHECK(rms_db(interior(filtered)) <= rms_db(interior(raw)) - 60);
}

TEST_CASE("interpolation then decimation is near identity for band-limited input", "[multirate]") {
    auto x = signals::io::sine(500, 0.5, 0, 8000, 4000);
    auto up = interpolate(x, 3, {});
    REQUIRE(up);
    CHECK(up->size() == 12'000);
    auto back = decimate(*up, 3, {});
    REQUIRE(back);
    REQUIRE(back->size() == x.size());
    double err = 0;
    for (std::size_t k = 200; k < 3800; ++k) err = std::max(err, std::abs((*back)[k] - x[k]));
    CHECK(err <= 1e-3);
}

TEST_CASE("zero factors are errors and unfiltered resampling is exact", "[multirate]") {
    const std::vector<double> x{1, 2, 3, 4, 5};
    CHECK(decimate(x, 0, {}).error().code == Errc::invalid_argument);
    CHECK(interpolate(x, 0, {}).error().code == Errc::invalid_argument);
    CHECK(*decimate(x, 2, {false}) == std::vector<double>{1, 3, 5});
    CHECK(*interpolate(std::vector<double>{1, 2}, 3, {false}) == std::vector<double>{1, 0, 0, 2, 0, 0});
    CHECK(*decimate(x, 1, {}) == x);
    const auto h = anti_alias_filter(4, {});
    CHECK(h.coeffs.size() % 2 == 1);
}
