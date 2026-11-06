#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
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

TEST_CASE("a failed write never deletes what is already at the path", "[wav]") {
    namespace fs = std::filesystem;
    const auto path = temp_path("readonly.wav");
    if (fs::exists(path)) fs::permissions(path, fs::perms::owner_all, fs::perm_options::add);
    fs::remove(path);
    {
        std::ofstream keep{path};
        keep << "keep";
    }
    fs::permissions(path, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    const auto r = write_wav(path, AudioBuffer{8000, {{0.1, 0.2}}}, {});
    const auto content = file_bytes(path);
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::add);
    if (r) SKIP("running with permission to overwrite read-only files");
    CHECK(r.error().code == Errc::io_error);
    CHECK(std::string(content.begin(), content.end()) == "keep");
    const auto dir = temp_path("outdir");
    fs::create_directories(dir);
    CHECK(write_wav(dir, AudioBuffer{8000, {{0.1}}}, {}).error().code == Errc::io_error);
    CHECK(fs::is_directory(dir));
}

TEST_CASE("high sample rates and the channel limit round trip", "[wav]") {
    const auto path = temp_path("768k.wav");
    REQUIRE(write_wav(path, AudioBuffer{768'000, {sine(1000, 0.5, 0.0, 768'000, 768)}}, {}));
    auto back = read_wav(path);
    REQUIRE(back);
    CHECK(back->info.sample_rate == 768'000);
    const AudioBuffer many{8000, std::vector<std::vector<double>>(256, std::vector<double>(4, 0.1))};
    REQUIRE(write_wav(path, many, {SampleFormat::pcm_s16, false}));
    CHECK(read_wav(path)->info.channels == 256);
    const AudioBuffer too_many{8000, std::vector<std::vector<double>>(257, std::vector<double>(4, 0.1))};
    CHECK(write_wav(path, too_many, {}).error().code == Errc::invalid_argument);
}

namespace {

struct StopNow final : signals::core::ProgressSink {
    void report(double) override {}
    bool cancelled() const override { return true; }
};

}  // namespace

TEST_CASE("a cancelled write leaves the old file and no temporary files", "[wav][cancel]") {
    const auto dir = temp_path("wav-cancel");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "out.wav";
    {
        std::ofstream old{path};
        old << "old";
    }
    AudioBuffer audio;
    audio.sample_rate = 8000;
    audio.channels.push_back(sine(440, 0.5, 0, 8000, 200000));
    StopNow stop;
    const auto r = write_wav(path, audio, {}, &stop);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::cancelled);
    std::ifstream in{path};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text == "old");
    CHECK(std::distance(std::filesystem::directory_iterator{dir}, std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("writing replaces an existing file in one step", "[wav]") {
    const auto dir = temp_path("wav-replace");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "out.wav";
    {
        std::ofstream old{path};
        old << "old";
    }
    AudioBuffer audio;
    audio.sample_rate = 8000;
    audio.channels.push_back(sine(440, 0.5, 0, 8000, 800));
    REQUIRE(write_wav(path, audio, {}));
    CHECK(std::filesystem::file_size(path) > 3);
    CHECK(std::distance(std::filesystem::directory_iterator{dir}, std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("the temporary file has an unpredictable name and is never reused", "[wav]") {
    const auto dir = temp_path("wav-temp-name");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "out.wav";
    const auto decoy = dir / ".out.wav.partial";
    {
        std::ofstream keep{decoy};
        keep << "keep";
    }
    AudioBuffer audio;
    audio.sample_rate = 8000;
    audio.channels.push_back(sine(440, 0.5, 0, 8000, 800));
    REQUIRE(write_wav(path, audio, {}));
    std::ifstream in{decoy};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text == "keep");
    CHECK(std::distance(std::filesystem::directory_iterator{dir}, std::filesystem::directory_iterator{}) == 2);
}
