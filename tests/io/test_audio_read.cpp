#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "core/progress.hpp"
#include "io/audio_file.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"
#include "support/paths.hpp"

using signals::core::Errc;
using signals::io::Container;
using signals::io::probe_audio;
using signals::io::read_audio;
using signals::test::audio_fixture;

namespace {

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t n = 0; n < std::min(a.size(), b.size()); ++n) {
        ab += a[n] * b[n];
        aa += a[n] * a[n];
        bb += b[n] * b[n];
    }
    return ab / std::sqrt(aa * bb);
}

double rms(const std::vector<double>& a) { return std::sqrt(std::inner_product(a.begin(), a.end(), a.begin(), 0.0) / static_cast<double>(a.size())); }

}  // namespace

TEST_CASE("flac decodes to exactly the samples of the wav it was encoded from", "[audio][flac]") {
    const auto wav = signals::io::read_wav(audio_fixture("tone_stereo.wav"));
    const auto flac = read_audio(audio_fixture("tone_stereo.flac"));
    REQUIRE(wav);
    REQUIRE(flac);
    CHECK(flac->info.container == Container::flac);
    CHECK(flac->info.format == signals::io::SampleFormat::pcm_s16);
    CHECK(flac->info.bits == 16);
    CHECK(flac->info.channels == 2);
    CHECK(flac->info.sample_rate == 44100);
    CHECK(flac->info.frames == 4410);
    CHECK(flac->audio.sample_rate == 44100.0);
    CHECK(flac->audio.channels == wav->audio.channels);
}

TEST_CASE("24-bit flac keeps its resolution and 6-channel flac keeps its channel order", "[audio][flac]") {
    const auto mono = read_audio(audio_fixture("tone_mono24.flac"));
    REQUIRE(mono);
    CHECK(mono->info.format == signals::io::SampleFormat::pcm_s24);
    CHECK(mono->info.bits == 24);
    CHECK(mono->info.sample_rate == 48000);
    REQUIRE(mono->audio.channels.size() == 1);
    REQUIRE(mono->audio.frames() == 2400);
    for (std::size_t n = 0; n < 2400; ++n)
        CHECK(mono->audio.channels[0][n] == Catch::Approx(0.7 * std::sin(2 * M_PI * 250 * static_cast<double>(n) / 48000)).margin(1.5 / 8388608.0));
    const auto six = read_audio(audio_fixture("tone_6ch.flac"));
    REQUIRE(six);
    REQUIRE(six->audio.channels.size() == 6);
    CHECK(six->audio.frames() == 1000);
    for (std::size_t c = 0; c < 6; ++c) {
        double peak = 0;
        for (double v : six->audio.channels[c]) peak = std::max(peak, std::abs(v));
        CHECK(peak == Catch::Approx(0.1 * static_cast<double>(c + 1)).margin(2e-3));
    }
}

TEST_CASE("a flac stream whose header does not give its length is counted before it is read", "[audio][flac]") {
    const auto info = probe_audio(audio_fixture("tone_stream.flac"));
    REQUIRE(info);
    CHECK(info->frames == 4410);
    const auto flac = read_audio(audio_fixture("tone_stream.flac"));
    REQUIRE(flac);
    CHECK(flac->info.frames == 4410);
    CHECK(flac->audio.channels == read_audio(audio_fixture("tone_stereo.flac"))->audio.channels);
}

TEST_CASE("mp3 decodes with the encoder delay removed and the channels in order", "[audio][mp3]") {
    const auto wav = signals::io::read_wav(audio_fixture("tone_stereo.wav"));
    const auto mp3 = read_audio(audio_fixture("tone_stereo.mp3"));
    const auto ffmpeg = signals::io::read_wav(audio_fixture("tone_stereo_mp3_ffmpeg.wav"));
    REQUIRE(wav);
    REQUIRE(mp3);
    REQUIRE(ffmpeg);
    CHECK(mp3->info.container == Container::mp3);
    CHECK(mp3->info.channels == 2);
    CHECK(mp3->info.sample_rate == 44100);
    CHECK(mp3->info.frames == 4410);
    REQUIRE(mp3->audio.channels.size() == 2);
    for (std::size_t c = 0; c < 2; ++c) {
        INFO("channel " << c);
        CHECK(correlation(mp3->audio.channels[c], wav->audio.channels[c]) > 0.99);
        REQUIRE(ffmpeg->audio.channels[c].size() == mp3->audio.channels[c].size());
        double worst = 0;
        for (std::size_t n = 0; n < mp3->audio.channels[c].size(); ++n) worst = std::max(worst, std::abs(mp3->audio.channels[c][n] - ffmpeg->audio.channels[c][n]));
        // dr_mp3 and ffmpeg differ by up to 1.6e-5 here (float synthesis rounding); the codec's own error is ~1e-2.
        CHECK(worst < 1e-4);
        CHECK(rms(mp3->audio.channels[c]) == Catch::Approx(rms(ffmpeg->audio.channels[c])).epsilon(1e-5));
    }
    CHECK(correlation(mp3->audio.channels[0], wav->audio.channels[1]) < 0.2);
}

TEST_CASE("mp3 with an ID3v2 tag in front is still found and decoded", "[audio][mp3]") {
    const auto mp3 = read_audio(audio_fixture("tone_mono_id3.mp3"));
    REQUIRE(mp3);
    CHECK(mp3->info.container == Container::mp3);
    CHECK(mp3->info.channels == 1);
    CHECK(mp3->info.sample_rate == 48000);
    CHECK(mp3->info.frames == 2400);
}

TEST_CASE("the format is read from the file's content, not its extension", "[audio]") {
    const auto named = read_audio(audio_fixture("flac_named.wav"));
    REQUIRE(named);
    CHECK(named->info.container == Container::flac);
    CHECK(named->audio.channels == read_audio(audio_fixture("tone_stereo.flac"))->audio.channels);
    const auto wav = read_audio(audio_fixture("tone_stereo.wav"));
    REQUIRE(wav);
    CHECK(wav->info.container == Container::wav);
    const auto bad = read_audio(audio_fixture("not_audio.mp3"));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == Errc::invalid_argument);
    CHECK(bad.error().message.find("WAV, FLAC") != std::string::npos);
    CHECK(read_audio(audio_fixture("missing.flac")).error().code == Errc::io_error);
}

TEST_CASE("probing flac and mp3 gives the same facts as reading them", "[audio]") {
    for (const char* name : {"tone_stereo.wav", "tone_stereo.flac", "tone_mono24.flac", "tone_6ch.flac", "tone_stereo.mp3", "tone_mono_id3.mp3"}) {
        INFO(name);
        const auto info = probe_audio(audio_fixture(name));
        const auto file = read_audio(audio_fixture(name));
        REQUIRE(info);
        REQUIRE(file);
        CHECK(info->container == file->info.container);
        CHECK(info->channels == file->info.channels);
        CHECK(info->sample_rate == file->info.sample_rate);
        CHECK(info->frames == file->info.frames);
    }
    CHECK_FALSE(probe_audio(audio_fixture("not_audio.mp3")));
}

TEST_CASE("flac and mp3 reading report progress and can be cancelled", "[audio]") {
    struct StopAfterFirst : signals::core::ProgressSink {
        int calls = 0;
        void report(double) override { ++calls; }
        bool cancelled() const override { return calls > 0; }
    };
    for (const char* name : {"tone_stereo.flac", "tone_stereo.mp3"}) {
        INFO(name);
        StopAfterFirst stop;
        CHECK(read_audio(audio_fixture(name), &stop).error().code == Errc::cancelled);
        CHECK(stop.calls > 0);
    }
}

TEST_CASE("a header that claims more frames than the file holds does not make reading allocate them", "[audio][flac]") {
    const auto info = probe_audio(audio_fixture("tone_huge_header.flac"));
    REQUIRE(info);
    CHECK(info->frames == std::uint64_t{1} << 35);
    const auto flac = read_audio(audio_fixture("tone_huge_header.flac"));
    REQUIRE(flac);
    CHECK(flac->audio.frames() == 4410);
    const auto* type = signals::pipeline::find_block_type("wav");
    REQUIRE(type);
    auto params = signals::pipeline::resolve_params(*type, {{"path", audio_fixture("tone_huge_header.flac").string()}});
    REQUIRE(params);
    signals::pipeline::RunContext ctx{std::filesystem::temp_directory_path()};
    const auto out = type->run(ctx, {}, *params);
    REQUIRE_FALSE(out);
    CHECK(out.error().code == Errc::invalid_argument);
    CHECK(out.error().message.find("2²⁸") != std::string::npos);
}

TEST_CASE("the wav block of the pipeline opens flac and mp3 too", "[audio][pipeline]") {
    const auto* type = signals::pipeline::find_block_type("wav");
    REQUIRE(type);
    for (const char* name : {"tone_stereo.flac", "tone_stereo.mp3"}) {
        INFO(name);
        auto params = signals::pipeline::resolve_params(*type, {{"path", audio_fixture(name).string()}});
        REQUIRE(params);
        signals::pipeline::RunContext ctx{std::filesystem::temp_directory_path()};
        auto out = type->run(ctx, {}, *params);
        REQUIRE(out);
        const auto& audio = std::get<signals::core::AudioBuffer>(out->front());
        CHECK(audio.channels.size() == 2);
        CHECK(audio.frames() == 4410);
    }
}
