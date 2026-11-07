#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <numbers>
#include <fstream>
#include <iterator>
#include <variant>
#include <sstream>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "core/analysis.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/examples.hpp"
#include "core/check.hpp"
#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/multirate.hpp"
#include "core/process/shrink.hpp"
#include "io/generators.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"
#include "support/golden.hpp"
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
            CHECK(core::max_abs_error(ys.channels[0], ym.channels[0]) < (std::string_view{sigma} == "mad_d1" ? 1e-4 : 1e-9));
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


TEST_CASE("lift53 and ilift53 reconstruct, and the integer mode is lossless on 16-bit audio", "[pipeline][lifting]") {
    const auto x = test_wav();
    for (const char* integer : {"no", "yes"}) {
        INFO(integer);
        const auto c = run("lift53", {x}, {{"levels", "5"}, {"integer", integer}});
        const auto& coeffs = std::get<Coeffs>(c[0]);
        CHECK(coeffs.levels() == 5);
        const auto d1 = band_port(coeffs, "d1");
        REQUIRE(d1);
        CHECK(std::get<core::AudioBuffer>(*d1).sample_rate == x.sample_rate / 2);
        const auto y = audio(run("ilift53", {c[0]})[0]);
        REQUIRE(y.channels[0].size() == x.channels[0].size());
        if (std::string_view{integer} == "yes") CHECK(y.channels[0] == x.channels[0]);
        else CHECK(core::max_abs_error(x.channels[0], y.channels[0]) < 1e-12);
        const auto* idwt = find_block_type("idwt");
        CHECK_FALSE(idwt->run(RunContext{}, c, *resolve_params(*idwt, {})));
    }
}

TEST_CASE("analysis blocks run per channel", "[pipeline][analysis]") {
    core::AudioBuffer x;
    x.sample_rate = 8000;
    x.channels = {{}, {}};
    for (std::size_t n = 0; n < 800; ++n) {
        x.channels[0].push_back(std::sin(2 * std::numbers::pi * 500 * static_cast<double>(n) / 8000));
        x.channels[1].push_back(0.5 * std::cos(2 * std::numbers::pi * 1000 * static_cast<double>(n) / 8000));
    }
    const auto env = audio(run("envelope", {x})[0]);
    CHECK(env.channels[0] == core::envelope(x.channels[0]));
    CHECK(env.sample_rate == 8000);
    const auto inst = audio(run("inst_freq", {x})[0]);
    CHECK(inst.channels[1].size() == 799);
    CHECK(std::abs(inst.channels[1][400] - 1000) < 1e-6);
    CHECK(audio(run("cepstrum", {x})[0]).channels[0] == core::real_cepstrum(x.channels[0]));
    const auto d = run("dct", {x});
    CHECK(core::max_abs_error(audio(run("idct", {d[0]})[0]).channels[1], x.channels[1]) < 1e-12);
    const auto c = audio(run("xcorr", {x, x}, {{"mode", "full"}, {"normalize", "yes"}})[0]);
    REQUIRE(c.channels[0].size() == 1599);
    CHECK(std::abs(c.channels[0][799] - 1.0) < 1e-12);
    CHECK(std::abs(c.channels[1][799] - 1.0) < 1e-12);
    const auto same = audio(run("xcorr", {x, x}, {{"mode", "same"}})[0]);
    CHECK(same.channels[0].size() == 800);
}

TEST_CASE("the denoise block spins cycles like the core", "[pipeline][analysis]") {
    const auto g = signals::test::load_golden("ops.json");
    core::AudioBuffer x;
    x.sample_rate = 1024;
    x.channels = {g["cycle_spin"]["noisy"].get<std::vector<double>>()};
    const auto spun = audio(run("denoise", {x}, {{"wavelet", "db4"}, {"levels", "5"}, {"rule", "bayes"}, {"mode", "soft"}, {"shifts", "8"}})[0]);
    CHECK(core::max_abs_error(spun.channels[0], g["cycle_spin"]["out"].get<std::vector<double>>()) < 1e-12);
    core::DenoiseOptions o;
    o.wavelet = "db4";
    o.levels = 5;
    const auto once = audio(run("denoise", {x}, {{"wavelet", "db4"}, {"levels", "5"}})[0]);
    CHECK(once.channels[0] == core::denoise(x.channels[0], o)->first);
}

TEST_CASE("thd and onsets blocks report their measurements", "[pipeline][analysis]") {
    core::AudioBuffer x;
    x.sample_rate = 48000;
    x.channels = {std::vector<double>(48000)};
    for (std::size_t n = 0; n < 48000; ++n) {
        const double t = static_cast<double>(n) / 48000;
        x.channels[0][n] = std::sin(2 * std::numbers::pi * 1000 * t) + 0.01 * std::sin(2 * std::numbers::pi * 2000 * t);
    }
    const auto r = std::get<Report>(run("thd", {x}, {{"harmonics", "3"}})[0]);
    auto field = [&](const Report& rep, const std::string& name) {
        for (const auto& [k, v] : rep.fields)
            if (k == name) return v;
        FAIL("no field " << name);
        return std::variant<double, std::string>{0.0};
    };
    CHECK(std::abs(std::get<double>(field(r, "f0_hz")) - 1000) < 0.01);
    CHECK(std::abs(std::get<double>(field(r, "thd_percent")) - 1.0) < 0.01);
    core::AudioBuffer clicks;
    clicks.sample_rate = 16000;
    clicks.channels = {std::vector<double>(32000, 0.0)};
    for (const double t0 : {0.5, 1.2})
        for (std::size_t k = 0; k < 80; ++k) clicks.channels[0][static_cast<std::size_t>(t0 * 16000) + k] = (k % 2 ? 0.8 : -0.8) * std::exp(-static_cast<double>(k) / 20);
    const auto o = std::get<Report>(run("onsets", {clicks})[0]);
    CHECK(std::get<double>(field(o, "count")) == 2);
    CHECK(std::get<std::string>(field(o, "times_s")).find(' ') != std::string::npos);
}

TEST_CASE("export writes csv and json that read back", "[pipeline][analysis]") {
    const auto dir = std::filesystem::temp_directory_path() / "signals-export-тест";
    std::filesystem::create_directories(dir);
    core::AudioBuffer x;
    x.sample_rate = 8000;
    x.channels = {{0.1, -0.25, 1.0 / 3}, {1, 2, 3}};
    run("export", {x}, {{"path", (dir / "x.csv").string()}, {"format", "csv"}});
    std::ifstream csv{dir / "x.csv"};
    std::string header, first;
    std::getline(csv, header);
    std::getline(csv, first);
    CHECK(header == "n,ch0,ch1");
    CHECK(first == "0,0.10000000000000001,1");
    run("export", {x}, {{"path", (dir / "x.json").string()}, {"format", "json"}});
    std::ifstream js{dir / "x.json"};
    const auto j = nlohmann::json::parse(js);
    CHECK(j["rate"] == 8000);
    CHECK(j["channels"][0][2].get<double>() == 1.0 / 3);
    Report rep;
    rep.fields = {{"snr_db", 12.5}, {"path", std::string{"a\"b"}}};
    run("export_report", {rep}, {{"path", (dir / "r.json").string()}, {"format", "json"}});
    std::ifstream rj{dir / "r.json"};
    const auto parsed = nlohmann::json::parse(rj);
    CHECK(parsed["fields"]["snr_db"] == 12.5);
    CHECK(parsed["fields"]["path"] == "a\"b");
    CHECK(parsed["passed"] == true);
    const auto* t = find_block_type("export");
    const auto params = resolve_params(*t, {{"path", (dir / "missing" / "x.csv").string()}});
    REQUIRE(params);
    CHECK_FALSE(t->run(RunContext{dir}, std::vector<Value>{x}, *params));
    CHECK_FALSE(std::filesystem::exists(dir / "missing"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("m6g review: exports are side effects, stream to disk and stop on cancel", "[pipeline][analysis]") {
    CHECK(find_block_type("export")->side_effect);
    CHECK(find_block_type("export_report")->side_effect);
    struct Stop final : core::ProgressSink {
        void report(double) override {}
        bool cancelled() const override { return true; }
    } stop;
    const auto dir = std::filesystem::temp_directory_path() / "signals-export-cancel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    core::AudioBuffer x;
    x.sample_rate = 48000;
    x.channels = {std::vector<double>(200000, 0.5)};
    const auto* t = find_block_type("export");
    const auto params = resolve_params(*t, {{"path", (dir / "big.csv").string()}});
    REQUIRE(params);
    RunContext ctx{dir};
    ctx.progress = &stop;
    const auto r = t->run(ctx, std::vector<Value>{x}, *params);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == core::Errc::cancelled);
    CHECK(std::filesystem::is_empty(dir));
    Report rep;
    rep.fields = {{"path", std::string{"/tmp/my \"best\", take.wav"}}};
    run("export_report", {rep}, {{"path", (dir / "r.csv").string()}, {"format", "csv"}});
    std::ifstream in{dir / "r.csv"};
    std::string header, line;
    std::getline(in, header);
    std::getline(in, line);
    CHECK(line == "path,\"/tmp/my \"\"best\"\", take.wav\"");
    std::filesystem::remove_all(dir);
}

TEST_CASE("m6g review: the cepstrum of silence is zero and gen scales the test signals", "[pipeline][analysis]") {
    for (double v : core::real_cepstrum(std::vector<double>(8, 0.0))) CHECK(v == 0);
    const auto y = audio(run("gen", {}, {{"kind", "bumps"}, {"rate", "1024"}, {"duration", "1"}, {"amplitude", "0.1"}})[0]);
    const auto ref = io::donoho_johnstone(io::DonohoJohnstone::bumps, 1024);
    for (std::size_t n = 0; n < ref.size(); n += 97) CHECK(y.channels[0][n] == 0.1 * ref[n]);
}

TEST_CASE("m7b: idwt and iswt name the inverse that fits the coefficients", "[pipeline][lifting]") {
    const auto x = test_wav();
    const std::vector<std::tuple<const char*, const char*, const char*>> cases{
        {"lift53", "idwt", "ilift53"}, {"lift53", "iswt", "ilift53"}, {"swt", "idwt", "iswt"}, {"dwt", "iswt", "idwt"}};
    for (const auto& [forward, inverse, expected] : cases) {
        INFO(forward << " → " << inverse);
        const auto c = run(forward, {x}, {{"levels", "3"}});
        const auto* block = find_block_type(inverse);
        const auto r = block->run(RunContext{}, c, *resolve_params(*block, {}));
        REQUIRE_FALSE(r);
        CHECK(r.error().message.find(std::string{"нужен "} + expected) != std::string::npos);
    }
}

TEST_CASE("m7b: xcorr refuses inputs with different sample rates", "[pipeline]") {
    core::AudioBuffer a, b;
    a.sample_rate = 8000;
    b.sample_rate = 16000;
    a.channels.push_back(std::vector<double>(64, 0.5));
    b.channels.push_back(std::vector<double>(64, 0.5));
    const auto* block = find_block_type("xcorr");
    const std::vector<Value> in{a, b};
    const auto r = block->run(RunContext{}, in, *resolve_params(*block, {}));
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("8000") != std::string::npos);
}

TEST_CASE("m7b review: swt and lift53 level errors name each unit once", "[pipeline][review]") {
    const auto x = test_wav();
    for (const auto language : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{language};
        const bool ru = language == signals::Language::ru;
        for (const char* name : {"swt", "lift53"}) {
            const auto* block = find_block_type(name);
            const auto params = resolve_params(*block, {{"levels", "30"}});
            REQUIRE(params);
            const std::vector<Value> in{x};
            const auto r = block->run(RunContext{}, in, *params);
            REQUIRE_FALSE(r);
            const auto& m = r.error().message;
            INFO(m);
            CHECK(m.find(ru ? "30 уровней" : "30 levels") != std::string::npos);
            CHECK(m.find(ru ? "отсчётов" : "samples") != std::string::npos);
            CHECK(m.find(ru ? "уровней 30" : "levels levels") == std::string::npos);
        }
    }
}

TEST_CASE("m7e: samples, add, phased downsampling and zero stuffing build a filter bank", "[pipeline][teaching]") {
    const auto x = audio(run("samples", {}, {{"values", "4 6 10 12 8 6 5 5"}, {"rate", "8"}})[0]);
    REQUIRE(x.channels.size() == 1);
    CHECK(x.channels[0] == std::vector<double>{4, 6, 10, 12, 8, 6, 5, 5});
    CHECK(x.sample_rate == 8);
    const auto odd = audio(run("downsample", {x}, {{"m", "2"}, {"anti_alias", "no"}, {"phase", "1"}})[0]);
    CHECK(odd.channels[0] == std::vector<double>{6, 12, 6, 5});
    CHECK(odd.sample_rate == 4);
    const auto stuffed = audio(run("upsample", {odd}, {{"m", "2"}, {"anti_image", "no"}})[0]);
    CHECK(stuffed.channels[0] == std::vector<double>{6, 0, 12, 0, 6, 0, 5, 0});
    core::AudioBuffer shorter = x;
    shorter.channels[0].resize(3);
    const auto sum = audio(run("add", {x, shorter})[0]);
    CHECK(sum.channels[0] == std::vector<double>{8, 12, 20, 12, 8, 6, 5, 5});
    const auto diff = audio(run("add", {x, x}, {{"op", "sub"}})[0]);
    CHECK(diff.channels[0] == std::vector<double>(8, 0.0));
    core::AudioBuffer late = x;
    late.channels[0].insert(late.channels[0].begin(), 0.0);
    const auto report = std::get<Report>(run("check", {x, late}, {{"kind", "pr"}, {"delay", "auto"}})[0]);
    CHECK(report.passed);
    const auto delay = std::find_if(report.fields.begin(), report.fields.end(), [](const auto& f) { return f.first == "delay"; });
    REQUIRE(delay != report.fields.end());
    CHECK(std::get<double>(delay->second) == 1);
}

TEST_CASE("m7e: every example graph runs and passes its checks", "[pipeline][teaching]") {
    for (const auto& entry : std::filesystem::directory_iterator{std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples"}) {
        if (entry.path().extension() != ".sig") continue;
        INFO(entry.path().filename().string());
        std::ostringstream out, err;
        const auto path = entry.path().string();
        std::ifstream text_in{entry.path()};
        const std::string text{std::istreambuf_iterator<char>{text_in}, std::istreambuf_iterator<char>{}};
        const auto out_file = (std::filesystem::temp_directory_path() / "signals-example-out.wav").string();
        std::vector<std::string_view> args{"signals", "run", path};
        if (text.find("wav_out(") != std::string::npos) {
            args.push_back("-o");
            args.push_back(out_file);
        }
        const int code = signals::cli::run_cli(args, out, err);
        INFO(out.str() << err.str());
        CHECK(code == 0);
    }
    for (const char* name : {"haar_bank.sig", "haar_two_levels.sig", "dwt_levels.sig", "effects_chain.sig"})
        CHECK(std::filesystem::exists(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / name));
    for (const auto& ex : signals::pipeline::examples()) {
        const auto key = "tui.pipeline.example." + std::string{ex.name};
        CHECK(signals::tr(key) != key);
    }
}

TEST_CASE("a bank from the user library works in dwt, idwt and convolve", "[pipeline][user_banks]") {
    const auto db2 = core::wavelet("db2");
    REQUIRE(db2);
    core::set_user_wavelets([db2](std::string_view name) -> std::optional<core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db2;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    CHECK(core::user_wavelet_names() == std::vector<std::string>{"my_bank"});
    CHECK(core::all_wavelet_names().back() == "my_bank");
    const auto x = audio(run("samples", {}, {{"values", "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"}, {"rate", "16"}})[0]);
    const auto c = run("dwt", {x}, {{"wavelet", "my_bank"}, {"levels", "2"}});
    const auto back = audio(run("idwt", {c[0]}, {{"wavelet", "my_bank"}})[0]);
    REQUIRE(back.channels[0].size() >= 16);
    for (std::size_t k = 0; k < 16; ++k) CHECK(back.channels[0][k] == Catch::Approx(x.channels[0][k]).margin(1e-9));
    const auto filtered = audio(run("convolve", {x}, {{"filter", "my_bank.h0"}, {"method", "direct"}})[0]);
    CHECK(filtered.channels[0].size() >= 16);
    CHECK_FALSE(core::wavelet("nobody").has_value());
    core::set_user_wavelets({}, {});
    CHECK(core::user_wavelet_names().empty());
    CHECK_FALSE(core::wavelet("my_bank").has_value());
}
