#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/adaptive.hpp"
#include "core/metrics.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double gap(const std::vector<double>& a, const std::vector<double>& b) { return a.size() == b.size() ? max_abs_error(a, b) : 1e300; }

}  // namespace

TEST_CASE("lms and nlms match the numpy reference", "[adaptive][golden]") {
    const auto g = signals::test::load_golden("adaptive.json");
    for (const auto& s : g["scenarios"]) {
        const auto x = s["x"].get<std::vector<double>>();
        const auto d = s["d"].get<std::vector<double>>();
        for (const auto& run : s["runs"]) {
            INFO(s["name"].get<std::string>() << " " << run["rule"].get<std::string>());
            const auto rule = run["rule"] == "lms" ? AdaptiveRule::lms : AdaptiveRule::nlms;
            const auto r = adaptive_filter(x, d, s["taps"].get<std::size_t>(), run["mu"].get<double>(), rule);
            REQUIRE(r);
            CHECK(gap(r->y, run["y"].get<std::vector<double>>()) < 1e-10);
            CHECK(gap(r->e, run["e"].get<std::vector<double>>()) < 1e-10);
            CHECK(gap(r->w, run["w"].get<std::vector<double>>()) < 1e-10);
        }
    }
}

TEST_CASE("nlms identifies the unknown system", "[adaptive]") {
    const auto g = signals::test::load_golden("adaptive.json");
    const auto& s = g["scenarios"][0];
    const auto r = adaptive_filter(s["x"].get<std::vector<double>>(), s["d"].get<std::vector<double>>(), 8, 0.5, AdaptiveRule::nlms);
    REQUIRE(r);
    const auto h = g["system"].get<std::vector<double>>();
    for (std::size_t k = 0; k < h.size(); ++k) CHECK(std::abs(r->w[k] - h[k]) < 0.01);
    for (std::size_t k = h.size(); k < r->w.size(); ++k) CHECK(std::abs(r->w[k]) < 0.01);
}

TEST_CASE("adaptive filters refuse bad input and divergence", "[adaptive]") {
    const std::vector<double> x(64, 1.0), d(64, 0.5), short_d(10, 0.5);
    CHECK_FALSE(adaptive_filter(x, short_d, 4, 0.1, AdaptiveRule::lms));
    CHECK_FALSE(adaptive_filter(x, d, 0, 0.1, AdaptiveRule::lms));
    CHECK_FALSE(adaptive_filter(x, d, 65, 0.1, AdaptiveRule::lms));
    CHECK_FALSE(adaptive_filter(x, d, 4, 0.0, AdaptiveRule::lms));
    std::vector<double> loud(4000);
    for (std::size_t i = 0; i < loud.size(); ++i) loud[i] = (i % 7) - 3.0;
    const auto diverged = adaptive_filter(loud, loud, 16, 1.5, AdaptiveRule::lms);
    REQUIRE_FALSE(diverged);
    CHECK(diverged.error().message.find("mu") != std::string::npos);
    const std::vector<double> silence(256, 0.0), quarter(256, 0.25);
    const auto quiet = adaptive_filter(silence, quarter, 8, 1.0, AdaptiveRule::nlms);
    REQUIRE(quiet);
    for (const double v : quiet->e) CHECK(v == 0.25);
}
