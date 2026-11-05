#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "core/metrics.hpp"
#include "io/generators.hpp"
#include "io/wav.hpp"
#include "support/paths.hpp"

using signals::core::AudioBuffer;
using signals::core::Errc;
using signals::core::max_abs_error;
using namespace signals::io;
using signals::test::temp_path;

namespace {

std::vector<char> file_bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("int formats round trip within one lsb", "[wav]") {
    AudioBuffer a{48'000, {sine(997, 0.5, 0.0, 48'000, 4'800), sine(440, 0.25, 0.0, 48'000, 4'800)}};
    for (auto [fmt, lsb] : {std::pair{SampleFormat::pcm_s16, 1.0 / 32768}, std::pair{SampleFormat::pcm_s24, 1.0 / 8388608},
                            std::pair{SampleFormat::pcm_s32, 1.0 / 2147483648}}) {
        const auto path = temp_path("rt.wav");
        INFO(to_string(fmt));
        REQUIRE(write_wav(path, a, {fmt, false}));
        auto back = read_wav(path);
        REQUIRE(back);
        CHECK(back->info.format == fmt);
        CHECK(back->info.sample_rate == 48'000);
        REQUIRE(back->audio.channels.size() == 2);
        for (std::size_t ch = 0; ch < 2; ++ch) CHECK(max_abs_error(back->audio.channels[ch], a.channels[ch]) <= 0.5 * lsb);
    }
}

TEST_CASE("float32 round trip is exact to float precision", "[wav]") {
    AudioBuffer a{44'100, {sine(1000, 0.8, 0.0, 44'100, 4'410)}};
    const auto path = temp_path("f32.wav");
    auto r = write_wav(path, a, {SampleFormat::float32, false});
    REQUIRE(r);
    CHECK(r->clipped_samples == 0);
    auto back = read_wav(path);
    REQUIRE(back);
    CHECK(back->info.format == SampleFormat::float32);
    CHECK(max_abs_error(back->audio.channels[0], a.channels[0]) <= 6e-8 * 0.8);
}

TEST_CASE("out of range samples are clipped and counted, never wrapped", "[wav]") {
    AudioBuffer a{44'100, {{2.1361, -1.2207, 0.5}}};
    const auto path = temp_path("clip.wav");
    auto r = write_wav(path, a, {SampleFormat::pcm_s16, false});
    REQUIRE(r);
    CHECK(r->clipped_samples == 2);
    auto back = *read_wav(path);
    CHECK(back.audio.channels[0][0] == 32767.0 / 32768);
    CHECK(back.audio.channels[0][1] == -1.0);
    CHECK(back.audio.channels[0][2] == 0.5);
}

TEST_CASE("non-finite audio is rejected and no file appears", "[wav]") {
    const auto path = temp_path("nan.wav");
    for (double bad : {std::nan(""), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        std::filesystem::remove(path);
        AudioBuffer a{44'100, {{0.1, bad, 0.2}}};
        CHECK(write_wav(path, a, {}).error().code == Errc::invalid_argument);
        CHECK_FALSE(std::filesystem::exists(path));
    }
}

TEST_CASE("malformed buffers and formats are rejected before writing", "[wav]") {
    const auto path = temp_path("bad.wav");
    std::filesystem::remove(path);
    CHECK(write_wav(path, AudioBuffer{44'100, {{0.1, 0.2}, {0.3}}}, {}).error().code == Errc::invalid_argument);
    CHECK(write_wav(path, AudioBuffer{44'100.5, {{0.1}}}, {}).error().code == Errc::invalid_argument);
    CHECK(write_wav(path, AudioBuffer{0, {{0.1}}}, {}).error().code == Errc::invalid_argument);
    CHECK(write_wav(path, AudioBuffer{44'100, {}}, {}).error().code == Errc::invalid_argument);
    CHECK(write_wav(path, AudioBuffer{44'100, {{0.1}}}, {SampleFormat::mulaw}).error().code == Errc::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK(write_wav("/nonexistent/dir/x.wav", AudioBuffer{44'100, {{0.1}}}, {}).error().code == Errc::io_error);
}

TEST_CASE("default write is 24-bit", "[wav]") {
    const auto path = temp_path("default.wav");
    REQUIRE(write_wav(path, AudioBuffer{44'100, {{0.1, -0.1}}}, {}));
    CHECK(read_wav(path)->info.format == SampleFormat::pcm_s24);
}

TEST_CASE("tpdf dither is deterministic and small", "[wav]") {
    const double lsb = 1.0 / 32768;
    AudioBuffer a{48'000, {sine(997, 0.3, 0.0, 48'000, 48'000)}};
    const auto p1 = temp_path("d1.wav"), p2 = temp_path("d2.wav"), p3 = temp_path("d3.wav");
    REQUIRE(write_wav(p1, a, {SampleFormat::pcm_s16, true, 7}));
    REQUIRE(write_wav(p2, a, {SampleFormat::pcm_s16, true, 7}));
    REQUIRE(write_wav(p3, a, {SampleFormat::pcm_s16, true, 8}));
    CHECK(file_bytes(p1) == file_bytes(p2));
    CHECK(file_bytes(p1) != file_bytes(p3));
    auto back = *read_wav(p1);
    CHECK(max_abs_error(back.audio.channels[0], a.channels[0]) <= 1.5 * lsb);
    double mean = 0;
    for (std::size_t k = 0; k < a.frames(); ++k) mean += back.audio.channels[0][k] - a.channels[0][k];
    CHECK(std::abs(mean / a.frames()) < 0.1 * lsb);
}
