#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/progress.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "python/script.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;
namespace fs = std::filesystem;

constexpr const char* kLevel = "# One level of a two-band bank\n"
                               "block bank_level(wavelet=haar) {\n"
                               "    in x\n"
                               "    out y, a, d\n"
                               "    node h0  = convolve(filter=$wavelet.h0, method=direct)\n"
                               "    node h1  = convolve(filter=$wavelet.h1, method=direct)\n"
                               "    node da  = downsample(m=2, anti_alias=no, phase=1)\n"
                               "    node dd  = downsample(m=2, anti_alias=no, phase=1)\n"
                               "    node ua  = upsample(m=2, anti_image=no)\n"
                               "    node ud  = upsample(m=2, anti_image=no)\n"
                               "    node f0  = convolve(filter=$wavelet.f0, method=direct)\n"
                               "    node f1  = convolve(filter=$wavelet.f1, method=direct)\n"
                               "    node sum = add()\n"
                               "    x -> h0 -> da -> ua -> f0 -> sum.a\n"
                               "    x -> h1 -> dd -> ud -> f1 -> sum.b\n"
                               "    da -> a\n"
                               "    dd -> d\n"
                               "    sum -> y\n"
                               "PROBE"
                               "}\n";

constexpr const char* kSource = "node x = samples(values=\"4 6 10 12 8 6 5 5 | 1 2 3 4 5 6 7 8\", rate=8)\n";

std::string level(bool inner_probe) {
    std::string text = kLevel;
    text.replace(text.find("PROBE"), 5, inner_probe ? "    probe sum as \"xh\"\n" : "");
    return text;
}

std::string bank(bool inner_probes, const std::string& lets = "", const std::string& second = "wavelet=db2") {
    return "pipeline \"b\" v2\n" + lets + level(inner_probes) + kSource + "node lv1 = bank_level()\nnode lv2 = bank_level(" + second + ")\n" +
           "x -> lv1.x\nlv1.a -> lv2.x\nprobe lv1.d as \"d1\"\nprobe lv2.y as \"y2\"\n" + (inner_probes ? "probe lv2/h0 as \"h0b\"\n" : "");
}

std::string read(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

python::Script script_for(const std::string& text, python::ScriptKind kind = python::ScriptKind::script, const std::string& name = "blocks.py") {
    const auto dir = signals::test::temp_path("py-blocks");
    fs::create_directories(dir);
    const auto loaded = pipeline::load_sig(text, {dir});
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    const auto graph = pipeline::build_graph(loaded->doc, dir);
    INFO((graph ? std::string{} : graph.error().message));
    REQUIRE(graph);
    core::CancelToken cancel;
    auto s = python::build_script(loaded->doc, *graph, {kind, dir / name, "blocks.sig", text}, cancel);
    INFO((s ? std::string{} : s.error().message));
    REQUIRE(s);
    return *s;
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

std::string python_exe() {
    const char* p = std::getenv("SIGNALS_PYTHON");
    return p ? p : "python3";
}

bool python_ready() { return std::system((python_exe() + " -c \"import numpy, scipy, pywt\" > " + std::string{SIGNALS_NULL_DEVICE} + " 2>&1").c_str()) == 0; }

fs::path write_script(const std::string& sig, const std::string& name, const std::string& stem = "graph") {
    const auto dir = signals::test::temp_path("py-blocks-run") / name;
    fs::create_directories(dir);
    std::ofstream{dir / "graph.sig", std::ios::binary} << sig;
    const auto [code, out, err] = signals::test::run_cli({"signals", "python", (dir / "graph.sig").string(), "-o", (dir / (stem + ".py")).string()});
    INFO(err);
    REQUIRE(code == 0);
    return dir / (stem + ".py");
}

std::pair<int, std::string> run_python(const fs::path& script) {
    const auto log = script.parent_path() / (script.stem().string() + ".log");
    const auto cmd = python_exe() + " -I \"" + script.string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(cmd.c_str());
    return {status, read(log)};
}

}  // namespace

TEST_CASE("one block function serves calls with different args", "[py-blocks]") {
    const auto s = script_for(bank(false));
    INFO(s.text);
    CHECK(count(s.text, "def bank_level(") == 1);
    CHECK(count(s.text, "_fs = bank_level(") == 2);
    CHECK(s.text.find("lv1/") == std::string::npos);
    CHECK(s.text.find("trace") == std::string::npos);
    CHECK(s.text.find("# One level of a two-band bank") != std::string::npos);
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto [code, out] = run_python(write_script(bank(false), "calls"));
    INFO(out);
    CHECK(code == 0);
}

TEST_CASE("a block function traces inner probes for the check", "[py-blocks]") {
    const auto s = script_for(bank(true));
    INFO(s.text);
    CHECK(count(s.text, "def bank_level(") == 1);
    CHECK(s.text.find("trace") != std::string::npos);
    CHECK(s.text.find("verify(\"lv1/xh\"") != std::string::npos);
    CHECK(s.text.find("verify(\"lv2/xh\"") != std::string::npos);
    CHECK(s.text.find("verify(\"h0b\"") != std::string::npos);
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto script = write_script(bank(true), "trace");
    const auto [code, out] = run_python(script);
    INFO(out);
    CHECK(code == 0);
    auto text = read(script);
    const auto at = text.find("filter_taps(f\"{wavelet_}.f0\")");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string{"filter_taps(f\"{wavelet_}.f0\")"}.size(), "filter_taps(f\"{wavelet_}.f0\") * 2");
    std::ofstream{script, std::ios::binary} << text;
    const auto [broken, why] = run_python(script);
    INFO(why);
    CHECK(broken != 0);
}

TEST_CASE("a block function argument is live", "[py-blocks]") {
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto jobs = signals::test::temp_path("py-blocks-live");
    fs::remove_all(jobs);
    const auto job = jobs / "call-arg";
    fs::create_directories(job);
    for (const auto& [stem, w] : {std::pair{"first", "haar"}, std::pair{"second", "db2"}}) {
        const auto script = write_script(bank(true, std::string{"let w = "} + w + "\n", "wavelet=$w"), "live", stem);
        fs::copy_file(script, job / (std::string{stem} + ".py"), fs::copy_options::overwrite_existing);
        fs::copy_file(script.parent_path() / (std::string{stem} + ".npz"), job / (std::string{stem} + ".npz"), fs::copy_options::overwrite_existing);
    }
    std::ofstream{job / "var.txt"} << "w";
    const auto log = jobs / "live.log";
    const auto cmd = python_exe() + " -I \"" + (fs::path{SIGNALS_SOURCE_DIR} / "tools" / "bridge" / "run_live.py").string() + "\" \"" + jobs.string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(cmd.c_str());
    INFO(read(log));
    CHECK(status == 0);
}

TEST_CASE("block functions nest and pass coefficients through ports", "[py-blocks]") {
    const std::string sig = "pipeline \"n\" v2\n" + level(true) +
                            "block analyze(w=db2, n=1) {\n    in x\n    out c, lo, hi\n    node t = dwt(wavelet=$w, levels=$n)\n    x -> t -> c\n    t.a -> lo\n    t.d1 -> hi\n}\n"
                            "block twice {\n    in x\n    out y\n    node first = bank_level()\n    node second = bank_level(wavelet=db2)\n"
                            "    x -> first.x\n    first.y -> second.x\n    second.y -> y\n}\n" +
                            kSource +
                            "node an = analyze(w=haar, n=2)\nnode back = idwt()\nnode tw = twice()\nnode g = gain(db=-6)\n"
                            "node an1 = analyze(w=haar)\nnode f1 = iir(type=lowpass, order=2, cutoff=0.5)\nnode f2 = iir(type=lowpass, order=2, cutoff=0.5)\nnode g1 = gain(db=-6)\n"
                            "x -> an.x\nan.c -> back\nan.lo -> g\nx -> tw\nx -> an1.x\nan1.lo -> f1\nan.lo -> f2\nan1.hi -> g1\n"
                            "probe back as \"back\"\nprobe tw as \"tw\"\nprobe g as \"g\"\nprobe f1 as \"f1\"\nprobe f2 as \"f2\"\nprobe g1 as \"g1\"\n";
    const auto s = script_for(sig);
    INFO(s.text);
    CHECK(count(s.text, "def bank_level(") == 1);
    CHECK(count(s.text, "def twice(") == 1);
    CHECK(count(s.text, "def analyze(") == 1);
    CHECK(s.text.find("verify(\"tw/first/xh\"") != std::string::npos);
    CHECK(s.text.find("def bank_level(") < s.text.find("def twice("));
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto [code, out] = run_python(write_script(sig, "nested"));
    INFO(out);
    CHECK(code == 0);
}

TEST_CASE("the notebook gets the block functions in one cell", "[py-blocks]") {
    const auto nb = nlohmann::json::parse(script_for(bank(true), python::ScriptKind::notebook, "blocks.ipynb").text);
    std::size_t functions = 0, calls = 0;
    for (const auto& cell : nb.at("cells")) {
        std::string text;
        for (const auto& line : cell.at("source")) text += line.get<std::string>();
        functions += count(text, "def bank_level(");
        calls += count(text, "_fs = bank_level(");
    }
    CHECK(functions == 1);
    CHECK(calls == 2);
}
