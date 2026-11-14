#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/progress.hpp"
#include "io/npy.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;
using signals::test::run_cli;
using signals::test::temp_path;

std::filesystem::path example(const std::string& name) { return std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / name; }

std::string text(const io::NpzEntries& e, const std::string& key) {
    const auto* a = io::np_find(e, key);
    INFO(key);
    REQUIRE(a);
    return std::get<std::vector<std::string>>(a->data).at(0);
}

pipeline::RunResult run_example(const std::string& name) {
    std::ifstream in{example(name)};
    const std::string source{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto doc = pipeline::parse_sig(source);
    REQUIRE(doc);
    const auto graph = pipeline::build_graph(*doc, example(name).parent_path());
    REQUIRE(graph);
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    auto result = pipeline::run_graph(*graph, cache, {}, cancel);
    REQUIRE(result);
    return std::move(*result);
}

}  // namespace

TEST_CASE("run --npz writes every probe under its name", "[cli-npz]") {
    const auto out = temp_path("probes.npz");
    std::filesystem::remove(out);
    auto [code, stdout_text, err] = run_cli({"signals", "run", example("first_steps.sig").string(), "--npz", out.string()});
    INFO(err);
    REQUIRE(code == 0);
    const auto e = io::read_npz(out);
    REQUIRE(e);
    const auto r = run_example("first_steps.sig");
    for (const std::string probe : {"noisy", "clean"}) {
        INFO(probe);
        const auto& audio = std::get<core::AudioBuffer>(r.probes.at(probe));
        const auto* x = io::np_find(*e, probe + ".x");
        REQUIRE(x);
        CHECK(std::get<std::vector<double>>(x->data) == audio.channels.at(0));
        CHECK(std::get<std::vector<double>>(io::np_find(*e, probe + ".rate")->data) == std::vector<double>{audio.sample_rate});
    }
}

TEST_CASE("run --probe-format npy writes npy for signals and npz otherwise", "[cli-npz]") {
    const auto dir = temp_path("probe-format");
    std::filesystem::remove_all(dir);
    auto [code, out, err] = run_cli({"signals", "run", example("compression.sig").string(), "--probe-dir", dir.string(), "--probe-format", "npy"});
    INFO(err);
    REQUIRE(code == 0);
    const auto coeffs = io::read_npz(dir / std::filesystem::path{u8"10 % коэффициентов.npz"});
    REQUIRE(coeffs);
    CHECK(text(*coeffs, "detail_sign") == "pywt");
    const auto signal = io::read_npy(dir / std::filesystem::path{u8"x̂.npy"});
    REQUIRE(signal);
    REQUIRE_FALSE(signal->shape.empty());
    CHECK(signal->shape.back() > 100);
    auto [bad, bout, berr] = run_cli({"signals", "run", example("compression.sig").string(), "--probe-dir", dir.string(), "--probe-format", "mat"});
    CHECK(bad != 0);
}

TEST_CASE("dwt -o out.npz writes pywt keys", "[cli-npz]") {
    const auto out = temp_path("dwt.npz");
    auto [code, text_out, err] = run_cli({"signals", "dwt", signals::test::sample_str("test.wav"), "--wavelet", "db2", "--levels", "3", "-o", out.string()});
    INFO(err);
    REQUIRE(code == 0);
    const auto e = io::read_npz(out);
    REQUIRE(e);
    for (const std::string key : {"cA3", "cD3", "cD2", "cD1", "wavelet", "mode", "rate", "transform", "length", "detail_sign"}) {
        INFO(key);
        CHECK(io::np_find(*e, key));
    }
    CHECK(io::np_find(*e, "cA3")->shape.size() == 2);
    CHECK(text(*e, "wavelet") == "db2");
}

TEST_CASE("filter info -o writes the bank and refuses other extensions", "[cli-npz]") {
    const auto out = temp_path("bank.npz");
    auto [code, text_out, err] = run_cli({"signals", "filter", "info", "db2", "-o", out.string()});
    INFO(err);
    REQUIRE(code == 0);
    const auto e = io::read_npz(out);
    REQUIRE(e);
    for (const std::string key : {"dec_lo", "dec_hi", "rec_lo", "rec_hi", "name"}) CHECK(io::np_find(*e, key));
    CHECK(text_out.find("bank.npz") != std::string::npos);
    auto [bad, bout, berr] = run_cli({"signals", "filter", "info", "db2", "-o", temp_path("bank.txt").string()});
    CHECK(bad == 2);
    CHECK(berr.find(".npz") != std::string::npos);
}

TEST_CASE("run --npz on a failing graph writes nothing", "[cli-npz]") {
    const auto sig = temp_path("broken.sig");
    {
        std::ofstream f{sig};
        f << "pipeline \"broken\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nnode y = cut(begin=500, end=100)\nx -> y\nprobe y as \"y\"\n";
    }
    const auto out = temp_path("broken.npz");
    std::filesystem::remove(out);
    auto [code, text_out, err] = run_cli({"signals", "run", sig.string(), "--npz", out.string()});
    CHECK(code == 1);
    CHECK_FALSE(std::filesystem::exists(out));
}
