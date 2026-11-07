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

TEST_CASE("stft power matches scipy", "[spectral][stft][golden]") {
    const auto golden = signals::test::load_golden("spectral.json");
    const auto x = golden["signal"].get<std::vector<double>>();
    for (auto& c : golden["stft"]) {
        const std::size_t n = c["n"], hop = c["hop"];
        INFO("n " << n);
        const auto s = stft_power(x, n, hop);
        REQUIRE(s);
        const auto expected = c["power"].get<std::vector<std::vector<double>>>();
        REQUIRE(s->frames == expected.size());
        REQUIRE(s->bins == n / 2 + 1);
        double peak = 0, worst = 0;
        for (std::size_t f = 0; f < s->frames; ++f)
            for (std::size_t b = 0; b < s->bins; ++b) {
                peak = std::max(peak, expected[f][b]);
                worst = std::max(worst, std::abs(s->power[f * s->bins + b] - expected[f][b]));
            }
        CHECK(worst <= 1e-12 * peak);
    }
    CHECK_FALSE(stft_power(std::vector<double>(100, 1.0), 256, 64));
    CHECK_FALSE(stft_power(x, 256, 0));
}

TEST_CASE("find_peaks matches scipy on prominence, distance and plateaus", "[spectral][peaks][golden]") {
    const auto golden = signals::test::load_golden("spectral.json");
    const auto db = golden["welch_db"].get<std::vector<double>>();
    for (auto& c : golden["peaks"]) {
        const double prominence = c["prominence"];
        const std::size_t distance = c["distance"];
        INFO("prominence " << prominence << " distance " << distance);
        const auto peaks = find_peaks(db, prominence, distance);
        const auto index = c["index"].get<std::vector<std::size_t>>();
        const auto prom = c["prominences"].get<std::vector<double>>();
        REQUIRE(peaks.size() == index.size());
        for (std::size_t i = 0; i < peaks.size(); ++i) {
            CHECK(peaks[i].index == index[i]);
            CHECK(std::abs(peaks[i].prominence - prom[i]) <= 1e-9);
        }
    }
    const auto plateau = golden["plateau"].get<std::vector<double>>();
    std::vector<std::size_t> found;
    for (const auto& p : find_peaks(plateau)) found.push_back(p.index);
    CHECK(found == golden["plateau_peaks"].get<std::vector<std::size_t>>());
}

TEST_CASE("notes, f0 and harmonics follow the canvas rules", "[spectral][peaks]") {
    CHECK(note_of(440).name == "A4");
    CHECK(note_of(440).cents == 0);
    CHECK(note_of(86.1).name == "F2");
    CHECK(std::abs(note_of(86.1).cents + 23) <= 1);
    CHECK(note_of(261.63).name == "C4");
    CHECK(note_of(466.16).name == "A♯4");
    std::vector<double> f, db;
    for (int i = 0; i <= 1000; ++i) {
        f.push_back(i * 2.0);
        db.push_back(-60.0);
    }
    for (const int k : {1, 2, 3}) db[static_cast<std::size_t>(50 * k)] = -6.0 * (k - 1);
    db[400] = 3;
    const auto peaks = find_peaks(db, 12, 8);
    const auto f0 = estimate_f0(f, db, peaks);
    REQUIRE(f0);
    CHECK(*f0 == 100.0);
    const auto h = harmonics(f, db, *f0, 4);
    REQUIRE(h.size() == 4);
    CHECK(h[0].k == 1);
    CHECK(h[1].f == 200.0);
    CHECK(h[2].db == -12.0);
    CHECK(h[3].db == -60.0);
}
