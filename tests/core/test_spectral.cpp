#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/metrics.hpp"
#include "core/progress.hpp"
#include "core/spectral.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("welch matches scipy", "[spectral][golden]") {
    const auto golden = signals::test::load_golden("spectral.json");
    const auto x = golden["signal"].get<std::vector<double>>();
    const double fs = golden["fs"];
    for (auto& c : golden["cases"]) {
        const std::size_t n = c["nperseg"], o = c["noverlap"];
        INFO("nperseg " << n << " noverlap " << o);
        auto p = welch_psd(x, fs, n, o);
        REQUIRE(p);
        const auto expected = c["p"].get<std::vector<double>>();
        REQUIRE(p->size() == expected.size());
        CHECK(welch_frequencies(fs, n) == c["f"].get<std::vector<double>>());
        const double peak = *std::max_element(expected.begin(), expected.end());
        CHECK(max_abs_error(*p, expected) <= 1e-12 * peak);
    }
}

TEST_CASE("welch rejects short signals and honours cancellation", "[spectral]") {
    std::vector<double> x(100, 1.0);
    CHECK(welch_psd(x, 8000, 256, 128).error().code == Errc::invalid_argument);
    CHECK(welch_psd(x, 8000, 64, 64).error().code == Errc::invalid_argument);
    CHECK(welch_psd(x, 8000, 1, 0).error().code == Errc::invalid_argument);
    struct Stop : ProgressSink {
        void report(double) override {}
        bool cancelled() const override { return true; }
    } stop;
    std::vector<double> y(100'000, 0.1);
    CHECK(welch_psd(y, 8000, 256, 128, &stop).error().code == Errc::cancelled);
    struct Record : ProgressSink {
        std::vector<double> seen;
        void report(double f) override { seen.push_back(f); }
        bool cancelled() const override { return false; }
    } rec;
    REQUIRE(welch_psd(y, 8000, 256, 128, &rec));
    REQUIRE_FALSE(rec.seen.empty());
    CHECK(std::is_sorted(rec.seen.begin(), rec.seen.end()));
    CHECK(rec.seen.back() == 1.0);
}
