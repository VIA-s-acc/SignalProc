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

TEST_CASE("huge factors and lengths are rejected before anything is allocated", "[pipeline]") {
    const auto rejects = [](std::string_view block, Params p) {
        INFO(block);
        CHECK_FALSE(resolve_params(*find_block_type(block), p));
    };
    rejects("upsample", {{"m", "100000000"}});
    rejects("upsample", {{"m", "0"}});
    rejects("downsample", {{"m", "-3"}});
    rejects("resample", {{"l", "4294967297"}});
    rejects("downsample", {{"attenuation_db", "1e9"}});
    rejects("gen", {{"duration", "1e12"}});
    rejects("gen", {{"rate", "0"}});
    rejects("delay", {{"samples", "1000000000000"}});
    rejects("dwt", {{"levels", "1000"}});
    CHECK(resolve_params(*find_block_type("upsample"), {{"m", "1024"}}));

    const Params seconds = *resolve_params(*find_block_type("delay"), {{"samples", "1e9s"}});
    CHECK_FALSE(param_samples(seconds, "samples", 48000));

    const std::vector<double> x(16, 1.0);
    CHECK_FALSE(core::interpolate(x, std::numeric_limits<std::size_t>::max() / 4, {}));
}

TEST_CASE("output lengths above the cap are refused before allocating", "[pipeline][review]") {
    CHECK(checked_length(1000, 4));
    CHECK(*checked_length(1000, 4) == 4000);
    const auto big = checked_length(300'000, 1024);
    REQUIRE_FALSE(big);
    CHECK(big.error().code == core::Errc::invalid_argument);
    CHECK_FALSE(checked_length(std::size_t{1} << 40, std::size_t{1} << 40));
}

TEST_CASE("sigma and gains are validated like numbers", "[pipeline][review]") {
    const auto* thr = find_block_type("threshold");
    REQUIRE(thr);
    CHECK(resolve_params(*thr, {{"sigma", "mad_d1"}}));
    CHECK(resolve_params(*thr, {{"sigma", "0.02"}}));
    CHECK_FALSE(resolve_params(*thr, {{"sigma", "0,02"}}));
    CHECK_FALSE(resolve_params(*thr, {{"sigma", "-1"}}));
    CHECK_FALSE(resolve_params(*thr, {{"sigma", "0"}}));
    const auto* edit = find_block_type("band_edit");
    REQUIRE(edit);
    CHECK(resolve_params(*edit, {{"gains_db", "d2=-6, a=3"}}));
    CHECK_FALSE(resolve_params(*edit, {{"gains_db", "d2=x"}}));
    CHECK_FALSE(resolve_params(*edit, {{"gains_db", "d2"}}));
}

TEST_CASE("idwt inherits the wavelet and edges of its coefficients and refuses others", "[pipeline][review]") {
    const auto x = test_wav();
    const auto c = run("dwt", {x}, {{"wavelet", "db8"}, {"levels", "4"}, {"ext", "periodization"}});
    const auto y = run("idwt", {c[0]});
    CHECK(core::max_abs_error(x.channels[0], audio(y[0]).channels[0]) < 1e-9);
    const auto* idwt = find_block_type("idwt");
    auto params = resolve_params(*idwt, {{"wavelet", "sym8"}});
    REQUIRE(params);
    const auto wrong = idwt->run(RunContext{}, c, *params);
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().message.find("db8") != std::string::npos);
    params = resolve_params(*idwt, {{"ext", "symmetric"}});
    REQUIRE(params);
    CHECK_FALSE(idwt->run(RunContext{}, c, *params));
}

TEST_CASE("swt then iswt reconstructs any length and refuses idwt", "[pipeline][swt]") {
    const auto x = test_wav();
    for (const char* norm : {"no", "yes"}) {
        const auto c = run("swt", {x}, {{"wavelet", "db2"}, {"levels", "3"}, {"norm", norm}});
        const auto& coeffs = std::get<Coeffs>(c[0]);
        CHECK(coeffs.transform == "swt");
        CHECK(coeffs.levels() == 3);
        const auto d1 = band_port(coeffs, "d1");
        REQUIRE(d1);
        CHECK(std::get<core::AudioBuffer>(*d1).sample_rate == x.sample_rate);
        const auto y = run("iswt", {c[0]});
        CHECK(audio(y[0]).channels[0].size() == x.channels[0].size());
        CHECK(core::max_abs_error(x.channels[0], audio(y[0]).channels[0]) < 1e-10);
        const auto* idwt = find_block_type("idwt");
        CHECK_FALSE(idwt->run(RunContext{}, c, *resolve_params(*idwt, {})));
    }
}

TEST_CASE("swt refuses levels beyond the signal length before padding", "[pipeline][swt][review]") {
    core::AudioBuffer a;
    a.sample_rate = 8000;
    a.channels.push_back(std::vector<double>(1000, 0.25));
    const auto* swt = find_block_type("swt");
    const auto params = resolve_params(*swt, {{"levels", "20"}});
    REQUIRE(params);
    const std::vector<Value> in{a};
    const auto r = swt->run(RunContext{}, in, *params);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == core::Errc::level_out_of_range);
}

TEST_CASE("modwt and swt thresholds give the same signal and energies", "[pipeline][swt][review]") {
    const auto x = test_wav();
    const auto s = run("swt", {x}, {{"wavelet", "db2"}, {"levels", "4"}, {"norm", "no"}});
    const auto m = run("swt", {x}, {{"wavelet", "db2"}, {"levels", "4"}, {"norm", "yes"}});
    for (const char* rule : {"bayes", "visu"})
        for (const char* sigma : {"mad_d1", "0.01"}) {
            INFO(rule << " sigma=" << sigma);
            const auto ys = audio(run("iswt", {run("threshold", {s[0]}, {{"rule", rule}, {"sigma", sigma}})[0]})[0]);
            const auto ym = audio(run("iswt", {run("threshold", {m[0]}, {{"rule", rule}, {"sigma", sigma}})[0]})[0]);
            CHECK(core::max_abs_error(ys.channels[0], ym.channels[0]) < (std::string_view{sigma} == "mad_d1" ? 1e-5 : 1e-9));
            CHECK(core::max_abs_error(ys.channels[0], x.channels[0]) > 1e-4);
        }
    const auto es = std::get<Report>(run("band_energy", {s[0]})[0]);
    const auto em = std::get<Report>(run("band_energy", {m[0]})[0]);
    REQUIRE(es.fields.size() == em.fields.size());
    for (std::size_t i = 0; i < es.fields.size(); ++i) CHECK(std::get<double>(es.fields[i].second) == Catch::Approx(std::get<double>(em.fields[i].second)).margin(1e-9));
}

TEST_CASE("swt refuses an output above the length cap", "[pipeline][swt][review]") {
    core::AudioBuffer a;
    a.sample_rate = 8000;
    a.channels.push_back(std::vector<double>(std::size_t{1} << 24, 0.25));
    const auto* swt = find_block_type("swt");
    const auto params = resolve_params(*swt, {{"levels", "16"}});
    REQUIRE(params);
    const std::vector<Value> in{std::move(a)};
    const auto r = swt->run(RunContext{}, in, *params);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == core::Errc::invalid_argument);
}

