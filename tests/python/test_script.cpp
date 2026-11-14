#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "io/generators.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/form.hpp"
#include "python/script.hpp"
#include "support/cli_run.hpp"
#include "support/golden_text.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

core::Result<python::Script> script_for(const std::string& text, const std::string& name) {
    const auto doc = pipeline::parse_sig(text);
    REQUIRE(doc);
    const auto dir = signals::test::temp_path("py-script");
    std::filesystem::create_directories(dir);
    const auto graph = pipeline::build_graph(*doc, dir);
    REQUIRE(graph);
    core::CancelToken cancel;
    return python::build_script(*doc, *graph, {python::ScriptKind::script, dir / (name + ".py"), name + ".sig", text}, cancel);
}

std::string without_version(std::string text) {
    const std::string v{core::version()};
    for (auto at = text.find(v); at != std::string::npos; at = text.find(v)) text.replace(at, v.size(), "X.Y.Z");
    return text;
}

std::filesystem::path write_sig(const std::string& name, const std::string& text) {
    const auto path = signals::test::temp_path(name);
    std::ofstream{path} << text;
    return path;
}

constexpr const char* kTiny =
    "pipeline \"tiny\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode y = gain(db=-6)\nx -> y\nprobe y as \"y\"\n";

}  // namespace

TEST_CASE("script for a tiny graph matches its golden text", "[py-script]") {
    {
        const LanguageScope ru{Language::ru};
        const auto s = script_for(kTiny, "tiny");
        REQUIRE(s);
        signals::test::check_golden_text("python/tiny_ru", without_version(s->text));
    }
    const LanguageScope en{Language::en};
    const auto s = script_for(kTiny, "tiny");
    REQUIRE(s);
    signals::test::check_golden_text("python/tiny_en", without_version(s->text));
}

TEST_CASE("the data file holds draws, not noise", "[py-script]") {
    const auto s = script_for("pipeline \"n\" v1\nnode x = gen(kind=white, rate=8000, duration=0.01, seed=3, channels=2)\nprobe x as \"x\"\n", "n");
    REQUIRE(s);
    const auto* draws = io::np_find(s->data, "x.draws");
    REQUIRE(draws);
    CHECK(draws->shape == std::vector<std::size_t>{2, 80});
    auto want = io::normal_draws(80, 3);
    const auto second = io::normal_draws(80, 4);
    want.insert(want.end(), second.begin(), second.end());
    CHECK(std::get<std::vector<double>>(draws->data) == want);
    CHECK(s->text.find("noise_from_draws(d, \"white\"") != std::string::npos);
}

TEST_CASE("the check covers probes, reports and side-effect inputs", "[py-script]") {
    const auto s = script_for("pipeline \"w\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nnode w = wav_out(path=\"w.wav\", bits=16)\nx -> w\n", "w");
    INFO((s ? std::string{} : s.error().message));
    REQUIRE(s);
    std::vector<std::string> what;
    for (const auto& c : s->checks) what.push_back(c.what);
    CHECK(std::find(what.begin(), what.end(), "w ← in") != what.end());
    CHECK(std::find(what.begin(), what.end(), "w · clipped") != what.end());
    CHECK(std::find(what.begin(), what.end(), "w · path") != what.end());
}

TEST_CASE("a graph with nothing to check is refused", "[py-script]") {
    const auto s = script_for("pipeline \"e\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\n", "e");
    REQUIRE_FALSE(s);
    CHECK(s.error().message == tr("python.nothing_to_check"));
}

TEST_CASE("signals python reports a graph error like signals run", "[py-script]") {
    const auto sig = write_sig("broken-py.sig", "pipeline \"b\" v1\nnode x = nosuchblock()\nprobe x as \"x\"\n");
    const auto py = signals::test::temp_path("broken-py.py");
    std::filesystem::remove(py);
    auto [rcode, rout, rerr] = signals::test::run_cli({"signals", "run", sig.string()});
    auto [pcode, pout, perr] = signals::test::run_cli({"signals", "python", sig.string()});
    CHECK(pcode == rcode);
    CHECK(perr == rerr);
    CHECK_FALSE(std::filesystem::exists(py));
}

TEST_CASE("signals python names an unsupported node", "[py-script]") {
    const auto sig = write_sig("unsupported-py.sig", "pipeline \"u\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nnode back = reverse()\nx -> back\nprobe back as \"b\"\n");
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string()});
    if (python::find_py_form("reverse")) SKIP("reverse has a form now");
    CHECK(code == 1);
    CHECK(err.find("back") != std::string::npos);
    CHECK(err.find("reverse") != std::string::npos);
}

TEST_CASE("signals python writes the script and its data", "[py-script]") {
    const auto sig = write_sig("tiny-cli.sig", kTiny);
    const auto py = signals::test::temp_path("tiny-cli.py");
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string()});
    INFO(err);
    REQUIRE(code == 0);
    CHECK(std::filesystem::exists(py));
    CHECK(std::filesystem::exists(signals::test::temp_path("tiny-cli.npz")));
    CHECK(out.find("tiny-cli.py") != std::string::npos);
    auto [ncode, nout, nerr] = signals::test::run_cli({"signals", "python", sig.string(), "-o", signals::test::temp_path("t.ipynb").string()});
    CHECK(ncode == 2);
}

TEST_CASE("only the blocks of the tolerance table weaken tolerance", "[py-script]") {
    const std::map<std::string, double> table{{"iir", 1e-8}, {"remez", 1e-8}, {"firls", 1e-10}, {"lms", 1e-10}, {"sst_band", 1e-10}, {"sst_ridge", 1e-10}, {"cepstrum", 1e-9}};
    for (const auto& f : python::py_forms()) {
        INFO(f.block);
        if (f.tol == 1e-12) continue;
        REQUIRE(table.contains(f.block));
        CHECK(table.at(f.block) == f.tol);
    }
}

TEST_CASE("the first two lines of a script are fixed", "[py-script]") {
    const auto s = script_for("pipeline \"coding: raw_unicode_escape\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nprobe x as \"x\"\n", "coding_raw_unicode_escape");
    REQUIRE(s);
    const auto first = s->text.substr(0, s->text.find('\n'));
    const auto rest = s->text.substr(first.size() + 1);
    const auto second = rest.substr(0, rest.find('\n'));
    CHECK(first == "# -*- coding: utf-8 -*-");
    CHECK(second.find("coding") == std::string::npos);
    CHECK(second.find("raw_unicode_escape") == std::string::npos);
}
