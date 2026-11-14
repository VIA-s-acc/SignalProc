#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "core/progress.hpp"
#include "io/audio_file.hpp"
#include "io/npy.hpp"
#include "io/npy_audio.hpp"
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

namespace {

using signals::io::NpArray;
using signals::io::np_scalar;
using signals::io::np_vector;

std::filesystem::path array_file(const std::string& name, const signals::io::NpzEntries& entries) {
    const auto path = signals::test::temp_path(name);
    REQUIRE(signals::io::write_npz(path, entries));
    return path;
}

}  // namespace

TEST_CASE("read_audio opens npy and npz by content", "[audio][npy]") {
    const auto path = array_file("stereo-as.wav", {{"x", NpArray{{2, 4}, std::vector<double>{0.1, 0.2, 0.3, 0.4, -0.1, -0.2, -0.3, -0.4}}}, {"rate", np_scalar(8000)}});
    const auto f = read_audio(path);
    REQUIRE(f);
    CHECK(f->info.container == Container::npz);
    CHECK(f->audio.sample_rate == 8000);
    REQUIRE(f->audio.channels.size() == 2);
    CHECK(f->audio.channels[1] == std::vector<double>{-0.1, -0.2, -0.3, -0.4});
    const auto p = probe_audio(path);
    REQUIRE(p);
    CHECK(p->channels == 2);
    CHECK(p->frames == 4);
    CHECK(p->sample_rate == 8000);
}

TEST_CASE("npy without a rate needs one", "[audio][npy]") {
    const auto path = signals::test::temp_path("mono.npy");
    REQUIRE(signals::io::write_npy(path, np_vector({0.5, -0.5, 0.25})));
    const auto p = probe_audio(path);
    REQUIRE(p);
    CHECK(p->container == Container::npy);
    CHECK(p->sample_rate == 0);
    CHECK(p->frames == 3);
    const auto f = read_audio(path);
    REQUIRE_FALSE(f);
    CHECK(f.error().message.find("rate") != std::string::npos);
    const auto g = signals::io::read_array_audio(path, {16000.0, ""});
    REQUIRE(g);
    CHECK(g->audio.sample_rate == 16000);
    CHECK(g->info.sample_rate == 16000);
    CHECK(g->audio.channels.at(0) == std::vector<double>{0.5, -0.5, 0.25});
}

TEST_CASE("npz key picks the array", "[audio][npy]") {
    const auto two = array_file("two.npz", {{"x", np_vector({1, 2, 3})}, {"y", np_vector({4, 5})}, {"rate", np_scalar(100)}});
    const auto y = signals::io::read_array_audio(two, {std::nullopt, "y"});
    REQUIRE(y);
    CHECK(y->audio.channels.at(0) == std::vector<double>{4, 5});
    const auto x = signals::io::read_array_audio(two, {std::nullopt, ""});
    REQUIRE(x);
    CHECK(x->audio.channels.at(0) == std::vector<double>{1, 2, 3});
    const auto missing = signals::io::read_array_audio(two, {std::nullopt, "z"});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("'z'") != std::string::npos);
    const auto one = array_file("one.npz", {{"data", np_vector({7, 8, 9, 10})}});
    const auto d = signals::io::read_array_audio(one, {100.0, ""});
    REQUIRE(d);
    CHECK(d->audio.channels.at(0).size() == 4);
    const auto ab = array_file("ab.npz", {{"a", np_vector({1})}, {"b", np_vector({2})}});
    const auto e = signals::io::read_array_audio(ab, {100.0, ""});
    REQUIRE_FALSE(e);
    CHECK(e.error().message.find("a, b") != std::string::npos);
}

TEST_CASE("npz rate contradicting the given rate is an error", "[audio][npy]") {
    const auto path = array_file("rated.npz", {{"x", np_vector({1, 2})}, {"rate", np_scalar(8000)}});
    const auto bad = signals::io::read_array_audio(path, {16000.0, ""});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("8000") != std::string::npos);
    CHECK(bad.error().message.find("16000") != std::string::npos);
    CHECK(signals::io::read_array_audio(path, {8000.0, ""}));
}

TEST_CASE("an (N, C) array is refused with a hint", "[audio][npy]") {
    const auto e = signals::io::audio_from_array(NpArray{{48000, 2}, std::vector<double>(96000, 0.0)}, 8000);
    REQUIRE_FALSE(e);
    CHECK(e.error().message.find("48000") != std::string::npos);
    CHECK(e.error().message.find("x.T") != std::string::npos);
    const auto path = signals::test::temp_path("frames-first.npy");
    REQUIRE(signals::io::write_npy(path, NpArray{{48000, 2}, std::vector<double>(96000, 0.0)}));
    const auto f = signals::io::read_array_audio(path, {8000.0, ""});
    REQUIRE_FALSE(f);
    CHECK(f.error().message.find("x.T") != std::string::npos);
    CHECK_FALSE(signals::io::audio_from_array(NpArray{{2, 2, 2}, std::vector<double>(8, 0.0)}, 8000));
    CHECK_FALSE(signals::io::audio_from_array(signals::io::np_text("no"), 8000));
}

TEST_CASE("signal entries are (N,) for mono and (C, N) for stereo", "[audio][npy]") {
    const auto mono = signals::io::signal_entries({8000, {{1, 2, 3}}});
    REQUIRE(signals::io::np_find(mono, "x"));
    CHECK(signals::io::np_find(mono, "x")->shape == std::vector<std::size_t>{3});
    CHECK(std::get<std::vector<double>>(signals::io::np_find(mono, "rate")->data) == std::vector<double>{8000});
    const auto stereo = signals::io::signal_entries({8000, {{1, 2}, {3, 4}}});
    CHECK(signals::io::np_find(stereo, "x")->shape == std::vector<std::size_t>{2, 2});
}
