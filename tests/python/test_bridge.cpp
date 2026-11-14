#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/families.hpp"
#include "core/progress.hpp"
#include "io/npy.hpp"
#include "io/npy_audio.hpp"
#include "pipeline/npz_values.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/block_graphs.hpp"
#include "python/form.hpp"
#include "python/script.hpp"
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
        const auto file = python::check_script_name(name + ".py") ? name : name + "-block";
        auto [code, out, err] = signals::test::run_cli({"signals", "python", utf8(sig), "-o", utf8(scripts / (file + ".py"))});
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

TEST_CASE("bridge files open in numpy and pywt", "[bridge]") {
    if (!python_ready()) {
        if (std::getenv("SIGNALS_REQUIRE_BRIDGE")) FAIL("python3 with numpy, scipy and PyWavelets is required: SIGNALS_REQUIRE_BRIDGE is set");
        SKIP("no python3 with numpy, scipy and PyWavelets");
    }
    const auto root = signals::test::temp_path("bridge-files");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "banks");
    const auto doc = pipeline::parse_sig(
        "pipeline \"f\" v1\nnode s1 = gen(kind=harmonics, freq=220, rate=8000, duration=0.25, channels=2, amplitude=0.5)\n"
        "node s2 = gen(kind=white, rate=8000, duration=0.25, channels=2, amplitude=0.1, seed=5)\nnode x = add()\ns1 -> x.a\ns2 -> x.b\n"
        "node d = dwt(wavelet=db4, levels=4, ext=periodization)\nnode s = swt(wavelet=db2, levels=3, norm=yes)\nnode w = wpt(wavelet=db3, levels=3, basis=full)\n"
        "x -> d\nx -> s\nx -> w\nprobe x as \"x\"\nprobe d as \"d\"\nprobe s as \"s\"\nprobe w as \"w\"\n");
    REQUIRE(doc);
    const auto graph = pipeline::build_graph(*doc, root);
    REQUIRE(graph);
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    const auto run = pipeline::run_graph(*graph, cache, {}, cancel);
    REQUIRE(run);
    REQUIRE(io::write_npy(root / "signal.npy", io::signal_entries(std::get<core::AudioBuffer>(run->probes.at("x"))).front().second));
    for (const char* name : {"d", "s", "w"}) {
        const auto e = pipeline::coeffs_entries(std::get<pipeline::Coeffs>(run->probes.at(name)));
        REQUIRE(e);
        REQUIRE(io::write_npz(root / (std::string{name == std::string{"d"} ? "dwt" : name == std::string{"s"} ? "swt" : "wpt"} + ".npz"), *e));
    }
    std::string by_name, by_bank;
    for (const auto& name : core::all_wavelet_names()) {
        const auto bank = core::wavelet(name);
        if (!bank) continue;
        REQUIRE(io::write_npz(root / "banks" / (std::string{name} + ".npz"), pipeline::bank_entries(*bank)));
        auto& list = python::by_filter_bank().contains(name) ? by_bank : by_name;
        list += (list.empty() ? "\"" : ", \"") + std::string{name} + "\"";
    }
    std::ofstream{root / "spec.json"} << "{\"by_name\": [" << by_name << "], \"by_filter_bank\": [" << by_bank << "]}\n";
    const auto log = root / "files.log";
    const auto cmd = python_exe() + " -I \"" + utf8(std::filesystem::path{SIGNALS_SOURCE_DIR} / "tools" / "bridge" / "check_files.py") + "\" \"" + utf8(root) + "\" > \"" +
                     utf8(log) + "\" 2>&1";
    const int code = std::system(cmd.c_str());
    INFO(read(log));
    CHECK(code == 0);
}
