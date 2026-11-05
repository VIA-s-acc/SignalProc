#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core/metrics.hpp"
#include "core/process/threshold.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("thresholds match pywt", "[threshold][golden]") {
    const auto golden = signals::test::load_golden("threshold.json");
    for (auto& c : golden["cases"]) {
        const auto x = c["x"].get<std::vector<double>>();
        const auto expected = c["y"].get<std::vector<double>>();
        const auto mode = c["mode"].get<std::string>();
        if (mode == "firm") {
            CHECK(max_abs_error(threshold_firm(x, c["low"], c["high"]), expected) <= 1e-15);
            continue;
        }
        INFO(mode << " value=" << c["value"] << " substitute=" << c["substitute"]);
        const auto m = parse_threshold_mode(mode);
        REQUIRE(m);
        CHECK(threshold(x, c["value"], *m, c["substitute"]) == expected);
    }
}

TEST_CASE("threshold propagates nan", "[threshold]") {
    for (auto mode : {ThresholdMode::soft, ThresholdMode::hard, ThresholdMode::garrote}) {
        auto y = threshold(std::vector<double>{std::nan(""), 2}, 1, mode);
        CHECK(std::isnan(y[0]));
        CHECK(std::isfinite(y[1]));
    }
    CHECK(threshold(std::vector<double>{std::nan(""), 2}, 1, ThresholdMode::soft)[1] == 1);
}

TEST_CASE("threshold modes parse and zero stays zero", "[threshold]") {
    CHECK(parse_threshold_mode("soft") == ThresholdMode::soft);
    CHECK(parse_threshold_mode("garotte") == ThresholdMode::garrote);
    CHECK_FALSE(parse_threshold_mode("medium"));
    for (auto mode : {ThresholdMode::soft, ThresholdMode::garrote})
        CHECK(threshold(std::vector<double>{0.0, 3.0}, 0.0, mode) == std::vector<double>{0.0, 3.0});
    CHECK(threshold_firm(std::vector<double>{0.0, 0.5, 2.5}, 1, 1) == std::vector<double>{0.0, 0.0, 2.5});
}
