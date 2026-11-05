#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

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

namespace {

std::vector<double> music_excerpt() {
    auto w = read_wav(signals::test::sample("test.wav"));
    REQUIRE(w);
    return {w->audio.channels[0].begin() + 100'000, w->audio.channels[0].begin() + 165'536};
}

std::vector<double> with_noise(const std::vector<double>& clean, double snr_db, std::uint64_t seed) {
    double e = 0;
    for (double v : clean) e += v * v;
    std::mt19937_64 rng{seed};
    std::normal_distribution<double> normal{0.0, std::sqrt(e / clean.size() / std::pow(10.0, snr_db / 10))};
    auto noisy = clean;
    for (double& v : noisy) v += normal(rng);
    return noisy;
}

}  // namespace

TEST_CASE("dwt band files carry the octave sample rates", "[cli][audio]") {
    const auto json_path = temp_path("c.json").string(), dir = temp_path("bands").string();
    std::filesystem::remove_all(dir);
    auto [code, out, err] = run_cli({"signals", "dwt", sample_str("test.wav"), "--wavelet", "db2", "--levels", "3",
                                     "--channel", "0", "-o", json_path, "--bands-dir", dir});
    INFO(err);
    REQUIRE(code == 0);
    const auto rate = [&](const char* name) { return read_wav(std::filesystem::path{dir} / name)->info.sample_rate; };
    CHECK(rate("ch0_d1.wav") == 22'050);
    CHECK(rate("ch0_d2.wav") == 11'025);
    CHECK(rate("ch0_d3.wav") == 5'513);
    CHECK(rate("ch0_a3.wav") == 5'513);
    std::ifstream in{json_path};
    const auto j = nlohmann::json::parse(in);
    CHECK(j["wavelet"] == "db2");
    CHECK(j["levels"] == 3);
    CHECK(j["sample_rate"] == 44'100);
    REQUIRE(j["channels"].size() == 1);
    const auto& bands = j["channels"][0]["bands"];
    REQUIRE(bands.size() == 4);
    CHECK(bands[0]["name"] == "a3");
    CHECK(bands[1]["name"] == "d3");
    CHECK(bands[1]["rate"].get<double>() == 5512.5);
    CHECK(bands[3]["name"] == "d1");
    CHECK(bands[3]["coeffs"].size() == (450'686 + 3) / 2);
}

TEST_CASE("denoise command improves a noisy file", "[cli][audio]") {
    const auto clean = music_excerpt();
    const auto noisy_path = temp_path("noisy.wav").string(), clean_path = temp_path("clean.wav").string();
    const auto out_path = temp_path("denoised.wav").string();
    using signals::io::SampleFormat;
    REQUIRE(signals::io::write_wav(noisy_path, {44'100, {with_noise(clean, 10.0, 7)}}, {SampleFormat::float32, false}));
    REQUIRE(signals::io::write_wav(clean_path, {44'100, {clean}}, {SampleFormat::float32, false}));
    auto [code, out, err] = run_cli({"signals", "denoise", noisy_path, "-o", out_path, "--levels", "5", "--reference",
                                     clean_path, "--json"});
    INFO(err);
    REQUIRE(code == 0);
    const auto j = nlohmann::json::parse(out);
    const auto& ch = j["channels"][0];
    CHECK(ch["snr_out"].get<double>() - ch["snr_in"].get<double>() >= 4.0);
    CHECK(ch["thresholds"].size() == 5);
    CHECK(ch["sigma"].get<double>() > 0);
    CHECK(read_wav(out_path)->info.format == SampleFormat::pcm_s24);
}

TEST_CASE("too short signal gives a clear error", "[cli][audio]") {
    const auto path = temp_path("short.wav").string(), out_path = temp_path("short_out.wav").string();
    REQUIRE(signals::io::write_wav(path, {8000, {std::vector<double>(10, 0.1)}}, {}));
    auto [code, out, err] = run_cli({"signals", "denoise", path, "--wavelet", "sym8", "-o", out_path});
    CHECK(code == 2);
    CHECK(err.find("слишком короткий") != std::string::npos);
    auto [dwt_code, dwt_out, dwt_err] = run_cli({"signals", "dwt", path, "--wavelet", "sym8", "--levels", "1", "-o",
                                                 temp_path("short.json").string()});
    CHECK(dwt_code == 2);
    CHECK(dwt_err.find("слишком короткий") != std::string::npos);
}

TEST_CASE("filter response of the d2 band of db2", "[cli]") {
    auto [code, out, err] = run_cli({"signals", "filter", "response", "db2", "--band", "d", "--level", "2", "--points",
                                     "1024", "--json"});
    INFO(err);
    REQUIRE(code == 0);
    const auto j = nlohmann::json::parse(out);
    const auto omega = j["omega"].get<std::vector<double>>();
    const auto db = j["magnitude_db"].get<std::vector<double>>();
    REQUIRE(omega.size() == 1024);
    const auto peak = std::max_element(db.begin(), db.end()) - db.begin();
    CHECK(omega[peak] / std::numbers::pi >= 0.25);
    CHECK(omega[peak] / std::numbers::pi <= 0.5);
    CHECK(j["group_delay"][0].is_string());
    CHECK(j["metrics"].contains("cutoff_3db"));
    auto [text_code, text, text_err] = run_cli({"signals", "filter", "response", "db2"});
    CHECK(text_code == 0);
    CHECK(text.find("db2") != std::string::npos);
}
