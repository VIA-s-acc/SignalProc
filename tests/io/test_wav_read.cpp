#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/progress.hpp"
#include "io/wav.hpp"
#include "support/golden.hpp"
#include "support/paths.hpp"

using signals::core::Errc;
using signals::io::read_wav;
using signals::test::fixture;
using signals::test::sample;

TEST_CASE("wav fixtures read exactly", "[wav][golden]") {
    auto g = signals::test::load_golden("wav.json");
    for (auto& c : g["cases"]) {
        auto w = read_wav(fixture(c["file"]));
        INFO(c["file"]);
        REQUIRE(w);
        CHECK(to_string(w->info.format) == c["format"].get<std::string>());
        CHECK(w->info.channels == c["channels"].get<unsigned>());
        CHECK(w->info.sample_rate == c["sample_rate"].get<unsigned>());
        CHECK(w->info.frames == c["frames"].get<std::uint64_t>());
        CHECK(w->info.extensible == c["extensible"].get<bool>());
        CHECK(w->audio.sample_rate == c["sample_rate"].get<double>());
        REQUIRE(w->audio.channels.size() == c["samples"].size());
        for (std::size_t ch = 0; ch < w->audio.channels.size(); ++ch)
            CHECK(w->audio.channels[ch] == c["samples"][ch].get<std::vector<double>>());
    }
}

TEST_CASE("invalid wav files are errors, not crashes", "[wav]") {
    const auto golden = signals::test::load_golden("wav.json");
    for (auto& name : golden["invalid"]) {
        INFO(name);
        REQUIRE(std::filesystem::exists(fixture(name)));
        auto w = read_wav(fixture(name));
        REQUIRE_FALSE(w);
        CHECK((w.error().code == Errc::io_error || w.error().code == Errc::invalid_argument));
        CHECK_FALSE(w.error().message.empty());
    }
    CHECK(read_wav("/nonexistent/x.wav").error().code == Errc::io_error);
}

TEST_CASE("test.wav keeps its two channels apart", "[wav]") {
    auto w = read_wav(sample("test.wav"));
    REQUIRE(w);
    CHECK(w->audio.channels.size() == 2);
    CHECK(w->audio.frames() == 450'686);
    CHECK(w->audio.sample_rate == 44'100.0);
    CHECK(w->audio.channels[0] != w->audio.channels[1]);
}

TEST_CASE("an audio buffer without channels has no frames", "[wav]") {
    CHECK(signals::core::AudioBuffer{}.frames() == 0);
}

TEST_CASE("wav reading reports progress and can be cancelled", "[wav]") {
    struct Record : signals::core::ProgressSink {
        std::vector<double> seen;
        void report(double f) override { seen.push_back(f); }
        bool cancelled() const override { return false; }
    } rec;
    auto w = read_wav(sample("test.wav"), &rec);
    REQUIRE(w);
    REQUIRE_FALSE(rec.seen.empty());
    CHECK(std::is_sorted(rec.seen.begin(), rec.seen.end()));
    CHECK(rec.seen.back() == 1.0);
    struct StopAfterFirst : signals::core::ProgressSink {
        int calls = 0;
        void report(double) override { ++calls; }
        bool cancelled() const override { return calls > 0; }
    } stop;
    CHECK(read_wav(sample("test.wav"), &stop).error().code == Errc::cancelled);
}

TEST_CASE("probing a wav reads only its header", "[wav]") {
    const auto info = signals::io::probe_wav(signals::test::sample("test.wav"));
    REQUIRE(info);
    CHECK(info->frames == 450'686);
    CHECK(info->channels == 2);
    CHECK(info->sample_rate == 44'100);
    CHECK(info->format == signals::io::SampleFormat::pcm_s16);
    CHECK_FALSE(signals::io::probe_wav(signals::test::fixture("not_a_wav.wav")));
}
