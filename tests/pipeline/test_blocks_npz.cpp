#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "io/npy.hpp"
#include "io/npy_audio.hpp"
#include "pipeline/block.hpp"
#include "pipeline/npz_values.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;
using namespace signals::pipeline;

std::filesystem::path npy_fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "npy" / name;
}

core::Result<std::vector<Value>> attempt(const std::string& id, std::vector<Value> in, const Params& given, std::filesystem::path base = {}) {
    const auto* t = find_block_type(id);
    REQUIRE(t);
    const auto params = resolve_params(*t, given);
    if (!params) return std::unexpected{params.error()};
    RunContext ctx;
    ctx.base_dir = base.empty() ? signals::test::temp_path("blocks-npz") : base;
    std::filesystem::create_directories(ctx.base_dir);
    return t->run(ctx, in, *params);
}

std::vector<Value> run(const std::string& id, std::vector<Value> in, const Params& given, std::filesystem::path base = {}) {
    auto out = attempt(id, std::move(in), given, std::move(base));
    INFO((out ? std::string{} : out.error().message));
    REQUIRE(out);
    return *out;
}

core::AudioBuffer stereo() {
    core::AudioBuffer a{8000, {std::vector<double>(300), std::vector<double>(300)}};
    for (std::size_t k = 0; k < 300; ++k) a.channels[0][k] = std::sin(0.07 * static_cast<double>(k)), a.channels[1][k] = 0.5 * std::cos(0.03 * static_cast<double>(k));
    return a;
}

double gap(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

}  // namespace

TEST_CASE("export writes npy that read_npy returns unchanged", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    const auto x = stereo();
    run("export", {Value{x}}, {{"path", "x.npy"}, {"format", "npy"}}, dir);
    const auto a = io::read_npy(dir / "x.npy");
    REQUIRE(a);
    CHECK(a->shape == std::vector<std::size_t>{2, 300});
    const auto back = io::audio_from_array(*a, 8000);
    REQUIRE(back);
    CHECK(back->channels == x.channels);
}

TEST_CASE("export_report writes npz with fields and passed", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    Report r;
    r.fields = {{"snr_db", 12.5}, {"kind", std::string{"snr"}}};
    r.passed = true;
    run("export_report", {Value{r}}, {{"path", "r.npz"}, {"format", "npz"}}, dir);
    const auto e = io::read_npz(dir / "r.npz");
    REQUIRE(e);
    CHECK(std::get<std::vector<double>>(io::np_find(*e, "snr_db")->data) == std::vector<double>{12.5});
    CHECK(std::get<std::vector<double>>(io::np_find(*e, "passed")->data) == std::vector<double>{1});
}

TEST_CASE("wav reads npz with its rate and npy with the rate parameter", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    REQUIRE(io::write_npz(dir / "s.npz", io::signal_entries(stereo())));
    const auto a = std::get<core::AudioBuffer>(run("wav", {}, {{"path", "s.npz"}}, dir).front());
    CHECK(a.sample_rate == 8000);
    CHECK(a.channels == stereo().channels);
    REQUIRE(io::write_npy(dir / "m.npy", io::np_vector({0.25, -0.25, 0.5})));
    const auto none = attempt("wav", {}, {{"path", "m.npy"}}, dir);
    REQUIRE_FALSE(none);
    CHECK(none.error().message.find("rate") != std::string::npos);
    const auto m = std::get<core::AudioBuffer>(run("wav", {}, {{"path", "m.npy"}, {"rate", "16000"}}, dir).front());
    CHECK(m.sample_rate == 16000);
    REQUIRE(io::write_npz(dir / "k.npz", {{"left", io::np_vector({1, 2})}, {"right", io::np_vector({3, 4})}, {"rate", io::np_scalar(100)}}));
    const auto k = std::get<core::AudioBuffer>(run("wav", {}, {{"path", "k.npz"}, {"key", "right"}}, dir).front());
    CHECK(k.channels.at(0) == std::vector<double>{3, 4});
}

TEST_CASE("wav rate contradicting the file rate is an error", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    REQUIRE(io::write_npz(dir / "s.npz", io::signal_entries(stereo())));
    const auto bad = attempt("wav", {}, {{"path", "s.npz"}, {"rate", "44100"}}, dir);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("8000") != std::string::npos);
}

TEST_CASE("coeffs_file then idwt restores the signal", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    const auto x = stereo();
    const auto c = run("dwt", {Value{x}}, {{"wavelet", "db3"}, {"levels", "3"}, {"ext", "symmetric"}});
    const auto e = coeffs_entries(std::get<Coeffs>(c.front()));
    REQUIRE(e);
    REQUIRE(io::write_npz(dir / "c.npz", *e));
    const auto read = run("coeffs_file", {}, {{"path", "c.npz"}}, dir);
    const auto y = std::get<core::AudioBuffer>(run("idwt", {read.front()}, {}).front());
    REQUIRE(y.channels.size() == 2);
    for (std::size_t ch = 0; ch < 2; ++ch) CHECK(gap(y.channels[ch], x.channels[ch]) <= 1e-12);
    CHECK(y.sample_rate == 8000);
}

TEST_CASE("coeffs_file reads a pywt file and takes wavelet from parameters", "[blocks-npz]") {
    const auto params = Params{{"path", npy_fixture("pywt_dwt_bare.npz").string()}, {"wavelet", "db2"}, {"mode", "symmetric"}, {"rate", "1000"}};
    const auto read = run("coeffs_file", {}, params);
    auto y = std::get<core::AudioBuffer>(run("idwt", {read.front()}, {}).front());
    const auto signal = io::read_npz(npy_fixture("pywt_dwt.npz"));
    REQUIRE(signal);
    const auto& want = std::get<std::vector<double>>(io::np_find(*signal, "signal")->data);
    REQUIRE(y.channels.at(0).size() >= want.size());
    y.channels.at(0).resize(want.size());
    CHECK(gap(y.channels[0], want) <= 1e-12);
    CHECK(y.sample_rate == 1000);
    const auto missing = attempt("coeffs_file", {}, {{"path", npy_fixture("pywt_dwt_bare.npz").string()}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("wavelet") != std::string::npos);
}

TEST_CASE("coeffs_file refuses bands that do not fit the wavelet", "[blocks-npz]") {
    const auto dir = signals::test::temp_path("blocks-npz");
    auto e = io::read_npz(npy_fixture("pywt_dwt.npz"));
    REQUIRE(e);
    for (auto& [key, a] : *e)
        if (key == "wavelet") a = io::np_text("db4");
    REQUIRE(io::write_npz(dir / "wrong.npz", *e));
    const auto bad = attempt("coeffs_file", {}, {{"path", "wrong.npz"}}, dir);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("coeffs_file") != std::string::npos);
}

TEST_CASE("coeffs_file checks the declared size before reading", "[blocks-npz]") {
    const auto bad = attempt("coeffs_file", {}, {{"path", npy_fixture("huge_claim.npz").string()}, {"wavelet", "db2"}, {"mode", "symmetric"}, {"rate", "8000"}});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("2²⁸") != std::string::npos);
}
