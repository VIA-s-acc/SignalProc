#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "io/stats.hpp"
#include "io/wav.hpp"
#include "support/paths.hpp"

using namespace signals::io;
using signals::test::sample;

TEST_CASE("test.wav statistics match the design canvas", "[stats]") {
    auto s = channel_stats(*read_wav(sample("test.wav")));
    REQUIRE(s.size() == 2);
    CHECK(s[0].clipped == 1797);
    CHECK(s[1].clipped == 1856);
    CHECK(s[0].peak_dbfs == Catch::Approx(0.0).margin(1e-9));
    CHECK(s[0].rms_dbfs == Catch::Approx(-10.6).margin(0.05));
    CHECK(s[1].rms_dbfs == Catch::Approx(-10.6).margin(0.05));
    CHECK(s[0].crest_factor_db == Catch::Approx(s[0].peak_dbfs - s[0].rms_dbfs).margin(1e-12));
}

TEST_CASE("silence has minus infinity levels", "[stats]") {
    WavFile f{{SampleFormat::pcm_s16, 1, 8000, 64, false}, {8000, {std::vector<double>(64, 0.0)}}};
    auto s = channel_stats(f);
    REQUIRE(s.size() == 1);
    CHECK(s[0].peak_dbfs == -std::numeric_limits<double>::infinity());
    CHECK(s[0].rms_dbfs == -std::numeric_limits<double>::infinity());
    CHECK(s[0].crest_factor_db == 0.0);
    CHECK(s[0].clipped == 0);
    CHECK(s[0].dc == 0.0);
}

TEST_CASE("a full-scale square wave has 0 dB crest factor and a known dc", "[stats]") {
    WavFile f{{SampleFormat::float32, 1, 8000, 4, false}, {8000, {{0.5, -0.5, 0.5, 0.5}}}};
    auto s = channel_stats(f)[0];
    CHECK(s.peak_dbfs == Catch::Approx(20 * std::log10(0.5)).margin(1e-12));
    CHECK(s.crest_factor_db == Catch::Approx(0.0).margin(1e-12));
    CHECK(s.dc == 0.25);
    CHECK(s.clipped == 0);
}

TEST_CASE("float samples clip at full scale, integer ones one lsb below", "[stats]") {
    WavFile fl{{SampleFormat::float32, 1, 8000, 3, false}, {8000, {{1.0, -1.25, 0.999}}}};
    CHECK(channel_stats(fl)[0].clipped == 2);
    WavFile s16{{SampleFormat::pcm_s16, 1, 8000, 3, false}, {8000, {{32767.0 / 32768, -1.0, 32766.0 / 32768}}}};
    CHECK(channel_stats(s16)[0].clipped == 2);
}
