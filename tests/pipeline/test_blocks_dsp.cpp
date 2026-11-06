#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "core/check.hpp"
#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/multirate.hpp"
#include "core/process/shrink.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"
#include "support/paths.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

std::vector<Value> run(std::string_view type, std::vector<Value> inputs, Params params = {}) {
    const auto* t = find_block_type(type);
    REQUIRE(t);
    auto resolved = resolve_params(*t, params);
    INFO(type << ": " << (resolved ? std::string{} : resolved.error().message));
    REQUIRE(resolved);
    RunContext ctx{std::filesystem::temp_directory_path()};
    auto out = t->run(ctx, inputs, *resolved);
    INFO(type << ": " << (out ? std::string{} : out.error().message));
    REQUIRE(out);
    return *out;
}

const core::AudioBuffer& audio(const Value& v) { return std::get<core::AudioBuffer>(v); }

core::AudioBuffer test_wav() { return io::read_wav(signals::test::sample("test.wav"))->audio; }

std::string bytes(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("dwt then idwt reconstructs the signal", "[pipeline]") {
    const auto x = test_wav();
    const auto c = run("dwt", {x}, {{"wavelet", "db2"}, {"levels", "6"}, {"ext", "symmetric"}});
    REQUIRE(type_of(c[0]) == PortType::coeffs);
    const auto y = run("idwt", {c[0]}, {{"wavelet", "db2"}, {"ext", "symmetric"}});
    double peak = 0;
    for (double v : x.channels[0]) peak = std::max(peak, std::abs(v));
    CHECK(core::max_abs_error(x.channels[0], audio(y[0]).channels[0]) <= core::default_tolerance(6, 4, peak));
    CHECK(audio(y[0]).channels[0].size() == x.channels[0].size());
}

TEST_CASE("dwt, threshold and idwt equal core::denoise", "[pipeline]") {
    const auto x = test_wav();
    const auto c = run("dwt", {x}, {{"wavelet", "db2"}, {"ext", "symmetric"}});
    const auto t = run("threshold", {c[0]}, {{"rule", "bayes"}, {"mode", "soft"}});
    const auto y = run("idwt", {t[0]}, {{"wavelet", "db2"}, {"ext", "symmetric"}});
    core::DenoiseOptions o;
    o.wavelet = "db2";
    const auto direct = core::denoise(x, o);
    REQUIRE(direct);
    for (std::size_t ch = 0; ch < x.channels.size(); ++ch) CHECK(audio(y[0]).channels[ch] == direct->first.channels[ch]);
}

TEST_CASE("band ports are views of the coefficients", "[pipeline]") {
    const auto x = test_wav();
    const auto c = run("dwt", {x}, {{"wavelet", "db2"}, {"levels", "4"}});
    const auto& coeffs = std::get<Coeffs>(c[0]);
    const auto d3 = band_port(coeffs, "d3");
    REQUIRE(d3);
    CHECK(audio(*d3).channels[0] == coeffs.channels[0].bands[2]);
    CHECK(band_port(coeffs, "a"));
    CHECK_FALSE(band_port(coeffs, "d5"));
    CHECK_FALSE(band_port(coeffs, "x1"));
}

TEST_CASE("the pr check passes on a round trip and fails on damage", "[pipeline]") {
    const auto x = test_wav();
    const auto c = run("dwt", {x}, {{"wavelet", "db2"}, {"levels", "6"}});
    auto y = audio(run("idwt", {c[0]}, {{"wavelet", "db2"}})[0]);
    const auto ok = std::get<Report>(run("check", {x, y}, {{"kind", "pr"}, {"tol", "1e-9"}})[0]);
    CHECK(ok.passed);
    y.channels[0][100] += 0.01;
    const auto bad = std::get<Report>(run("check", {x, y}, {{"kind", "pr"}, {"tol", "1e-9"}})[0]);
    CHECK_FALSE(bad.passed);
    const auto snr = std::get<Report>(run("check", {x, y}, {{"kind", "snr"}, {"tol", "20"}})[0]);
    CHECK(snr.passed);
}

TEST_CASE("wav_out without dither writes what io::write_wav writes", "[pipeline]") {
    const auto x = test_wav();
    const auto dir = std::filesystem::temp_directory_path() / "signals-pipeline-wav-out";
    std::filesystem::create_directories(dir);
    run("wav_out", {x}, {{"path", (dir / "a.wav").string()}, {"bits", "24"}, {"dither", "none"}});
    io::WavWriteOptions o;
    o.format = io::SampleFormat::pcm_s24;
    o.dither = false;
    REQUIRE(io::write_wav(dir / "b.wav", x, o));
    CHECK(bytes(dir / "a.wav") == bytes(dir / "b.wav"));
    CHECK(find_block_type("wav_out")->side_effect);
}

TEST_CASE("multirate, convolution and band blocks match core", "[pipeline]") {
    const auto x = test_wav();
    const auto down = audio(run("downsample", {x}, {{"m", "2"}})[0]);
    CHECK(down.channels[0] == *core::decimate(x.channels[0], 2, core::MultirateOptions{}));
    CHECK(down.sample_rate == x.sample_rate / 2);
    CHECK(audio(run("upsample", {x}, {{"m", "3"}})[0]).sample_rate == x.sample_rate * 3);
    const auto conv = audio(run("convolve", {x}, {{"filter", "db2.h0"}})[0]);
    const auto h = core::wavelet("db2")->h0;
    CHECK(conv.channels[0] == core::convolve(core::Laurent{x.channels[0], 0}, h).coeffs);
    const auto c = run("dwt", {x}, {{"wavelet", "db2"}, {"levels", "3"}});
    const auto energy = std::get<Report>(run("band_energy", {c[0]})[0]);
    double total = 0;
    for (const auto& [name, v] : energy.fields)
        if (name.starts_with("c0.")) total += std::get<double>(v);
    CHECK(total == Catch::Approx(1).margin(1e-6));
    const auto edited = run("band_edit", {c[0]}, {{"enabled", "a,d3,d2"}, {"gains_db", "d2=-6"}});
    const auto& e = std::get<Coeffs>(edited[0]);
    for (double v : e.channels[0].bands[3]) CHECK(v == 0);
    CHECK(e.channels[0].bands[2][0] == Catch::Approx(std::get<Coeffs>(c[0]).channels[0].bands[2][0] * std::pow(10.0, -6.0 / 20)));
}
