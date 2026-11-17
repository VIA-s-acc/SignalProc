#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/progress.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "python/script.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

constexpr const char* kLevels = "pipeline \"v\" v2\n"
                                "let lv = 4\n"
                                "node x = gen(kind=sine, freq=440, rate=8000, duration=0.05)\n"
                                "node d = dwt(wavelet=db2, levels=$lv)\n"
                                "x -> d\n"
                                "probe d.a as \"a\"\n";

std::string read(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

python::Script script_for(const std::string& text, python::ScriptKind kind = python::ScriptKind::script, const std::string& name = "live.py") {
    const auto loaded = pipeline::load_sig(text);
    REQUIRE(loaded);
    const auto dir = signals::test::temp_path("py-live");
    std::filesystem::create_directories(dir);
    const auto graph = pipeline::build_graph(loaded->doc, dir);
    INFO((graph ? std::string{} : graph.error().message));
    REQUIRE(graph);
    core::CancelToken cancel;
    auto s = python::build_script(loaded->doc, *graph, {kind, dir / name, "live.sig", text}, cancel);
    INFO((s ? std::string{} : s.error().message));
    REQUIRE(s);
    return *s;
}

std::string python_exe() {
    const char* p = std::getenv("SIGNALS_PYTHON");
    return p ? p : "python3";
}

bool python_ready() { return std::system((python_exe() + " -c \"import numpy, scipy, pywt\" > " + std::string{SIGNALS_NULL_DEVICE} + " 2>&1").c_str()) == 0; }

struct Ran {
    int code = -1;
    std::string out;
};

std::filesystem::path write_script(const std::string& sig, const std::string& name) {
    const auto dir = signals::test::temp_path("py-live-run") / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream{dir / "graph.sig", std::ios::binary} << sig;
    const auto [code, out, err] = signals::test::run_cli({"signals", "python", (dir / "graph.sig").string(), "-o", (dir / "graph_live.py").string()});
    INFO(err);
    REQUIRE(code == 0);
    return dir / "graph_live.py";
}

Ran run_python(const std::filesystem::path& script) {
    const auto log = script.parent_path() / "run.log";
    const auto cmd = python_exe() + " -I \"" + script.string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(cmd.c_str());
    return {status, read(log)};
}

void replace_line(const std::filesystem::path& file, const std::string& from, const std::string& to) {
    auto text = read(file);
    const auto at = text.find("\n" + from + "\n");
    REQUIRE(at != std::string::npos);
    text.replace(at + 1, from.size(), to);
    std::ofstream{file, std::ios::binary} << text;
}

}  // namespace

TEST_CASE("a param that uses a variable is written as the variable", "[py-live]") {
    const auto s = script_for(kLevels);
    CHECK(s.text.find("\nlv = 4\n") != std::string::npos);
    CHECK(s.text.find("level=(lv or default_levels(x.shape[1], \"db2\")),") != std::string::npos);
    CHECK(s.text.find("level=4") == std::string::npos);
    CHECK(s.text.find("GRAPH = {\"lv\": 4}") != std::string::npos);
    CHECK(s.text.find("graph_changed(") != std::string::npos);
}

TEST_CASE("variable expressions become python in dependency order", "[py-live]") {
    const auto s = script_for("pipeline \"v\" v2\n"
                              "let half = \"$fs/2\"\n"
                              "let fs = 8000\n"
                              "let k = 2\n"
                              "let p = \"2^$k\"\n"
                              "let r = \"sqrt($k)\"\n"
                              "let w = db4\n"
                              "let dir = \"a$$b\"\n"
                              "node x = gen(kind=sine, freq=\"$half/10\", rate=$fs, duration=0.05)\n"
                              "node g = gain(db=\"-$p\")\n"
                              "x -> g\n"
                              "probe g as \"g\"\n");
    const auto at = [&](const char* line) { return s.text.find(std::string{"\n"} + line + "\n"); };
    CHECK(at("fs = 8000") != std::string::npos);
    CHECK(at("half = fs / 2") != std::string::npos);
    CHECK(at("fs = 8000") < at("half = fs / 2"));
    CHECK(at("p = 2 ** k") != std::string::npos);
    CHECK(at("r = np.sqrt(k)") != std::string::npos);
    CHECK(at("w = \"db4\"") != std::string::npos);
    CHECK(at("dir_ = \"a$b\"") != std::string::npos);
    CHECK(s.text.find("GRAPH = {\"half\": 4000, \"fs\": 8000, \"k\": 2, \"p\": 4,") != std::string::npos);
}

TEST_CASE("python reserved names are renamed", "[py-live]") {
    const std::string sig = "pipeline \"v\" v2\n"
                            "let np = 3\n"
                            "let lambda = 2\n"
                            "let GRAPH = 1\n"
                            "node x = gen(kind=sine, freq=440, rate=8000, duration=0.05)\n"
                            "node d = dwt(wavelet=db2, levels=$np)\n"
                            "x -> d\n"
                            "probe d.a as \"a\"\n";
    const auto s = script_for(sig);
    CHECK(s.text.find("\nnp_ = 3\n") != std::string::npos);
    CHECK(s.text.find("\nlambda_ = 2\n") != std::string::npos);
    CHECK(s.text.find("\nGRAPH_ = 1\n") != std::string::npos);
    CHECK(s.text.find("level=(np_ or default_levels(x.shape[1], \"db2\")),") != std::string::npos);
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto ran = run_python(write_script(sig, "reserved"));
    INFO(ran.out);
    CHECK(ran.code == 0);
}

TEST_CASE("a changed variable skips the check and exits 0", "[py-live]") {
    if (!python_ready()) SKIP("no python3 with numpy, scipy and PyWavelets");
    const auto script = write_script(kLevels, "changed");
    const auto same = run_python(script);
    INFO(same.out);
    CHECK(same.code == 0);
    CHECK(same.out.find(tr("python.live.changed")) == std::string::npos);
    replace_line(script, "lv = 4", "lv = 3");
    const auto changed = run_python(script);
    INFO(changed.out);
    CHECK(changed.code == 0);
    CHECK(changed.out.find(tr("python.live.changed")) != std::string::npos);
}

TEST_CASE("the notebook keeps the graph parameters in one code cell", "[py-live]") {
    const auto nb = nlohmann::json::parse(script_for(kLevels, python::ScriptKind::notebook, "live.ipynb").text);
    const auto source = [&](std::size_t k) {
        std::string out;
        for (const auto& line : nb.at("cells").at(k).at("source")) out += line.get<std::string>();
        return out;
    };
    CHECK(source(1).find("import numpy as np") != std::string::npos);
    CHECK(nb.at("cells").at(2).at("cell_type") == "markdown");
    CHECK(source(2).find(tr("python.notebook.vars")) != std::string::npos);
    CHECK(nb.at("cells").at(3).at("cell_type") == "code");
    CHECK(source(3).find("lv = 4\n") != std::string::npos);
    CHECK(source(3).find("GRAPH = {\"lv\": 4}") != std::string::npos);
    const auto last = source(nb.at("cells").size() - 1);
    CHECK(last.find("graph_changed(") != std::string::npos);
}
