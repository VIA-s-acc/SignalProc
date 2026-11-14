#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/block_graphs.hpp"
#include "python/form.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

std::string read(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return {u.begin(), u.end()};
}

std::string python_exe() {
    const char* p = std::getenv("SIGNALS_PYTHON");
    return p ? p : "python3";
}

bool python_ready() { return std::system((python_exe() + " -c \"import numpy, scipy, pywt\" > " + std::string{SIGNALS_NULL_DEVICE} + " 2>&1").c_str()) == 0; }

bool all_forms(const std::string& sig) {
    const auto doc = pipeline::parse_sig(sig);
    if (!doc) return false;
    for (const auto& n : doc->nodes)
        if (!python::find_py_form(n.type)) return false;
    return true;
}

std::vector<std::filesystem::path> example_files() {
    const auto dir = std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples";
    std::vector<std::filesystem::path> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator{dir})
        if (e.path().extension() == ".sig") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("every block graph runs in Signals", "[bridge-graphs]") {
    const auto dir = signals::test::temp_path("bridge-graphs");
    signals::test::prepare_block_files(dir);
    for (const auto& t : pipeline::block_types())
        for (const auto& g : signals::test::block_graphs(t, dir)) {
            INFO(g.name << "\n" << g.sig);
            const auto doc = pipeline::parse_sig(g.sig);
            REQUIRE(doc);
            const auto graph = pipeline::build_graph(*doc, dir);
            CHECK(graph);
            if (!graph) continue;
            pipeline::ResultCache cache{0};
            core::CancelToken cancel;
            const auto run = pipeline::run_graph(*graph, cache, {}, cancel);
            INFO((run ? std::string{} : run.error().message));
            CHECK(run);
        }
}

TEST_CASE("bridge scripts agree with Signals", "[bridge]") {
    if (!python_ready()) {
        if (std::getenv("SIGNALS_REQUIRE_BRIDGE")) FAIL("python3 with numpy, scipy and PyWavelets is required: SIGNALS_REQUIRE_BRIDGE is set");
        SKIP("no python3 with numpy, scipy and PyWavelets; set SIGNALS_PYTHON or install tools/requirements-bridge.txt");
    }
    const auto root = signals::test::temp_path("bridge");
    std::filesystem::remove_all(root);
    const auto scripts = root / std::filesystem::path{u8"скрипты с пробелом"};
    std::filesystem::create_directories(scripts);
    std::filesystem::create_directories(root / "sig");
    signals::test::prepare_block_files(root / "files");
    std::vector<std::string> skipped;
    std::size_t written = 0;
    const auto generate = [&](const std::filesystem::path& sig, const std::string& name) {
        auto [code, out, err] = signals::test::run_cli({"signals", "python", utf8(sig), "-o", utf8(scripts / (name + ".py"))});
        INFO(name << ": " << err);
        CHECK(code == 0);
        written += code == 0;
    };
    for (const auto& file : example_files()) {
        const auto name = utf8(file.parent_path().filename()) + "-" + utf8(file.stem());
        if (!all_forms(read(file))) {
            skipped.push_back(name);
            continue;
        }
        generate(file, name);
    }
    for (const auto& t : pipeline::block_types())
        for (const auto& g : signals::test::block_graphs(t, root / "files")) {
            if (!all_forms(g.sig)) {
                skipped.push_back(g.name);
                continue;
            }
            const auto sig = root / "sig" / (g.name + ".sig");
            std::ofstream{sig} << g.sig;
            generate(sig, g.name);
        }
    const auto log = root / "bridge.log";
    const auto cmd = python_exe() + " -I \"" + utf8(std::filesystem::path{SIGNALS_SOURCE_DIR} / "tools" / "bridge" / "run_scripts.py") + "\" \"" + utf8(scripts) + "\" > \"" +
                     utf8(log) + "\" 2>&1";
    const int code = std::system(cmd.c_str());
    const auto text = read(log);
    INFO(text.substr(text.size() > 6000 ? text.size() - 6000 : 0));
    CHECK(code == 0);
    std::string list;
    for (const auto& s : skipped) list += s + " ";
    UNSCOPED_INFO("generated " << written << ", skipped (blocks without a form) " << skipped.size() << ": " << list);
    CHECK(written > 0);
}
