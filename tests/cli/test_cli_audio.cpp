#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include "io/generators.hpp"
#include "io/wav.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

using signals::io::read_wav;
using signals::test::run_cli;
using signals::test::sample_str;
using signals::test::temp_path;

TEST_CASE("info reports test.wav like the design canvas", "[cli][audio]") {
    auto [code, out, err] = run_cli({"signals", "info", sample_str("test.wav"), "--json"});
    REQUIRE(code == 0);
    auto j = nlohmann::json::parse(out);
    CHECK(j["format"] == "pcm_s16");
    CHECK(j["channels"] == 2);
    CHECK(j["sample_rate"] == 44'100);
    CHECK(j["frames"] == 450'686);
    CHECK(j["duration"].get<double>() == 450'686.0 / 44'100);
    CHECK(j["extensible"] == false);
    CHECK(j["channels_stats"][0]["clipped"] == 1797);
    CHECK(j["channels_stats"][1]["clipped"] == 1856);
}

TEST_CASE("info prints minus infinity as a string for silence", "[cli][audio]") {
    const auto path = temp_path("silence.wav").string();
    REQUIRE(signals::io::write_wav(path, {8000, {std::vector<double>(64, 0.0)}}, {signals::io::SampleFormat::pcm_s16, false}));
    auto [code, out, err] = run_cli({"signals", "info", path, "--json"});
    REQUIRE(code == 0);
    auto j = nlohmann::json::parse(out);
    CHECK(j["channels_stats"][0]["peak_dbfs"] == "-inf");
    auto [text_code, text, text_err] = run_cli({"signals", "info", path});
    CHECK(text_code == 0);
    CHECK(text.find("-inf") != std::string::npos);
}

TEST_CASE("gen writes a sweep that reads back", "[cli][audio]") {
    const auto path = temp_path("sweep.wav").string();
    auto [code, out, err] = run_cli({"signals", "gen", "sweep", "--f0", "20", "--f1", "20000", "--log", "--rate", "48000",
                                     "--duration", "0.5", "-o", path});
    INFO(err);
    REQUIRE(code == 0);
    auto w = read_wav(path);
    REQUIRE(w);
    CHECK(w->info.frames == 24'000);
    CHECK(w->info.format == signals::io::SampleFormat::pcm_s24);
    const auto expected = signals::io::sweep(20, 20'000, signals::io::SweepKind::logarithmic, 48'000, 24'000);
    CHECK(std::abs(w->audio.channels[0][1000] - 0.5 * expected[1000]) <= std::ldexp(1.0, -23));
}

TEST_CASE("gen covers every kind and channel count", "[cli][audio]") {
    const auto path = temp_path("gen.wav").string();
    for (const char* kind : {"sine", "sweep", "impulse", "step", "noise", "harmonics", "am", "fm", "dj"}) {
        INFO(kind);
        auto [code, out, err] = run_cli({"signals", "gen", kind, "--rate", "8000", "--duration", "0.25", "--channels", "2",
                                         "--format", "f32", "-o", path});
        INFO(err);
        REQUIRE(code == 0);
        auto w = read_wav(path);
        REQUIRE(w);
        CHECK(w->info.channels == 2);
        CHECK(w->info.frames == 2000);
        CHECK(w->info.format == signals::io::SampleFormat::float32);
    }
    auto [bad, out, err] = run_cli({"signals", "gen", "chirpy", "-o", path});
    CHECK(bad == 2);
}

TEST_CASE("resample halves the rate", "[cli][audio]") {
    const auto in = temp_path("sine48k.wav").string(), out_path = temp_path("sine24k.wav").string();
    REQUIRE(std::get<0>(run_cli({"signals", "gen", "sine", "--rate", "48000", "--duration", "1", "-o", in})) == 0);
    auto [code, out, err] = run_cli({"signals", "resample", in, "--down", "2", "-o", out_path});
    INFO(err);
    REQUIRE(code == 0);
    auto w = read_wav(out_path);
    REQUIRE(w);
    CHECK(w->info.sample_rate == 24'000);
    CHECK(w->info.frames == 24'000);
    auto [up_code, up_out, up_err] = run_cli({"signals", "resample", in, "--up", "2", "--no-filter", "-o", out_path});
    REQUIRE(up_code == 0);
    CHECK(read_wav(out_path)->info.sample_rate == 96'000);
    CHECK(std::get<0>(run_cli({"signals", "resample", in, "-o", out_path})) == 2);
    CHECK(std::get<0>(run_cli({"signals", "resample", in, "--up", "2", "--down", "2", "-o", out_path})) == 2);
}

TEST_CASE("missing input file exits with 2 and a russian message", "[cli][audio]") {
    auto [code, out, err] = run_cli({"signals", "info", "/nope.wav"});
    CHECK(code == 2);
    CHECK(err.find("не удалось") != std::string::npos);
}
