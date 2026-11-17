#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/progress.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/script.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

constexpr const char* kGraph =
    "pipeline \"nb\" v2\n"
    "node x = gen(kind=sine, freq=440, rate=8000, duration=0.05)\n"
    "node y = gain(db=-6)\n"
    "node r = stats()\n"
    "x -> y\n"
    "y -> r\n"
    "probe y as \"quiet\"\n";

python::Script build(const std::string& text, python::ScriptKind kind, const std::string& name) {
    const auto doc = pipeline::parse_sig(text);
    REQUIRE(doc);
    const auto dir = signals::test::temp_path("py-notebook");
    std::filesystem::create_directories(dir);
    const auto graph = pipeline::build_graph(*doc, dir);
    REQUIRE(graph);
    core::CancelToken cancel;
    auto s = python::build_script(*doc, *graph, {kind, dir / name, "nb.sig", text}, cancel);
    INFO((s ? std::string{} : s.error().message));
    REQUIRE(s);
    return *s;
}

std::string joined(const nlohmann::json& cell) {
    std::string out;
    for (const auto& line : cell.at("source")) out += line.get<std::string>();
    return out;
}

std::vector<std::string> code_cells(const nlohmann::json& nb) {
    std::vector<std::string> out;
    for (const auto& c : nb.at("cells"))
        if (c.at("cell_type") == "code") out.push_back(joined(c));
    return out;
}

}  // namespace

TEST_CASE("notebook is valid nbformat 4 without outputs", "[py-notebook]") {
    const auto nb = nlohmann::json::parse(build(kGraph, python::ScriptKind::notebook, "nb.ipynb").text);
    CHECK(nb.at("nbformat") == 4);
    CHECK(nb.at("nbformat_minor") == 4);
    CHECK(nb.at("metadata").at("kernelspec").at("name") == "python3");
    CHECK(nb.at("metadata").at("language_info").at("name") == "python");
    REQUIRE(nb.at("cells").size() > 4);
    for (const auto& c : nb.at("cells")) {
        CHECK_FALSE(c.contains("id"));
        CHECK(c.at("metadata").is_object());
        REQUIRE(c.at("source").is_array());
        for (const auto& line : c.at("source")) CHECK(line.is_string());
        if (c.at("cell_type") == "code") {
            CHECK(c.at("outputs").empty());
            CHECK(c.at("execution_count").is_null());
        } else {
            CHECK(c.at("cell_type") == "markdown");
        }
    }
    const auto code = code_cells(nb);
    std::string all;
    for (const auto& c : code) all += c + "\n";
    CHECK(all.find("sys.exit") == std::string::npos);
    CHECK(code.back().find("assert report(") != std::string::npos);
    CHECK(all.find("D = np.load(") != std::string::npos);
    CHECK(all.find("__file__") == std::string::npos);
    CHECK(all.find("plot_signal(y, ") != std::string::npos);
}

TEST_CASE("notebook code equals the script code node by node", "[py-notebook]") {
    const auto script = build(kGraph, python::ScriptKind::script, "nb.py").text;
    const auto nb = nlohmann::json::parse(build(kGraph, python::ScriptKind::notebook, "nb.ipynb").text);
    std::vector<std::string> nodes;
    for (const auto& c : code_cells(nb))
        if (c.starts_with("# node ")) nodes.push_back(c);
    REQUIRE(nodes.size() == 3);
    std::size_t at = 0;
    for (const auto& n : nodes) {
        INFO(n);
        const auto found = script.find("\n" + n + (n.ends_with('\n') ? "" : "\n"), at);
        REQUIRE(found != std::string::npos);
        at = found + n.size();
    }
    std::string markdown;
    for (const auto& c : nb.at("cells"))
        if (c.at("cell_type") == "markdown") markdown += joined(c) + "\n";
    CHECK(markdown.find("stats") != std::string::npos);
}

TEST_CASE("listen becomes an audio widget in the notebook only", "[py-notebook]") {
    const std::string graph = std::string{kGraph} + "node l = listen()\ny -> l\n";
    const auto script = build(graph, python::ScriptKind::script, "nb.py").text;
    const auto nb = build(graph, python::ScriptKind::notebook, "nb.ipynb").text;
    CHECK(script.find("IPython") == std::string::npos);
    CHECK(nb.find("from IPython.display import Audio, display") != std::string::npos);
    CHECK(nb.find("display(Audio(y, rate=8000") != std::string::npos);
}

TEST_CASE("signals python writes a notebook for an .ipynb path", "[py-notebook]") {
    const auto sig = signals::test::temp_path("nb-cli.sig");
    {
        std::ofstream out{sig};
        out << kGraph;
    }
    const auto nb = signals::test::temp_path("nb-cli.ipynb");
    std::filesystem::remove(nb);
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string(), "-o", nb.string()});
    INFO(err);
    REQUIRE(code == 0);
    CHECK(std::filesystem::exists(nb));
    CHECK(std::filesystem::exists(signals::test::temp_path("nb-cli.npz")));
    CHECK(out.find("nb-cli.ipynb") != std::string::npos);
}
