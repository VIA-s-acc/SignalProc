#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core/metrics.hpp"
#include "core/process/shrink.hpp"
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

TEST_CASE("SURE and minimax thresholds match the reference", "[threshold][golden][sure]") {
    const auto golden = signals::test::load_golden("threshold.json");
    for (auto& c : golden["sure"]) {
        INFO(c["name"].get<std::string>());
        const auto x = c["x"].get<std::vector<double>>();
        const double sigma = c["sigma"];
        CHECK(std::abs(sure_threshold(x, sigma) - c["sure"].get<double>()) <= 1e-12);
    }
    for (auto& c : golden["minimax"]) {
        const std::size_t n = c["n"];
        INFO("n " << n);
        CHECK(std::abs(minimax_threshold(0.5, n) - 0.5 * c["lambda"].get<double>()) <= 1e-12);
    }
    CHECK(sure_threshold(std::vector<double>{}, 1.0) == 0);
    CHECK(sure_threshold(std::vector<double>{1, 2, 3}, 0) == 0);
    CHECK(minimax_threshold(0, 1000) == 0);
}

TEST_CASE("every rule gives a threshold through one entry point", "[threshold][sure]") {
    std::vector<double> band(256);
    for (std::size_t k = 0; k < band.size(); ++k) band[k] = std::sin(0.3 * static_cast<double>(k)) * 0.1;
    const double sigma = 0.05;
    CHECK(rule_threshold(ShrinkRule::bayes, band, sigma, 4096) == bayes_threshold(band, sigma));
    CHECK(rule_threshold(ShrinkRule::visu, band, sigma, 4096) == universal_threshold(sigma, 4096));
    CHECK(rule_threshold(ShrinkRule::sure, band, sigma, 4096) == sure_threshold(band, sigma));
    CHECK(rule_threshold(ShrinkRule::minimax, band, sigma, 4096) == minimax_threshold(sigma, 4096));
    CHECK(parse_shrink_rule("sure") == ShrinkRule::sure);
    CHECK(parse_shrink_rule("minimax") == ShrinkRule::minimax);
    CHECK(parse_shrink_rule("visu") == ShrinkRule::visu);
    CHECK_FALSE(parse_shrink_rule("other"));
    CHECK(shrink_rule_name(ShrinkRule::sure) == "SureShrink");
}
