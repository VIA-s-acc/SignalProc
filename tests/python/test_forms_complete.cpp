#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/progress.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/block_graphs.hpp"
#include "python/form.hpp"
#include "python/script.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

std::string script_error(const std::string& sig, const std::filesystem::path& base, const std::filesystem::path& out) {
    const auto doc = pipeline::parse_sig(sig);
    if (!doc) return "the graph does not parse";
    const auto graph = pipeline::build_graph(*doc, base);
    if (!graph) return graph.error().message;
    core::CancelToken cancel;
    const auto script = python::build_script(*doc, *graph, {python::ScriptKind::script, out, out.filename().replace_extension(".sig"), sig}, cancel);
    return script ? std::string{} : script.error().message;
}

std::vector<std::filesystem::path> examples() {
    std::vector<std::filesystem::path> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator{std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples"})
        if (e.path().extension() == ".sig") out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

}  // namespace

TEST_CASE("every block has a python form for every choice", "[py-complete]") {
    const auto dir = signals::test::temp_path("py-complete");
    signals::test::prepare_block_files(dir);
    for (const auto& t : pipeline::block_types()) {
        INFO(t.id);
        CHECK(python::find_py_form(t.id));
        for (const auto& g : signals::test::block_graphs(t, dir)) {
            INFO(g.name);
            CHECK(script_error(g.sig, dir, dir / (g.name + ".py")).empty());
        }
    }
}

TEST_CASE("bridge skips no example", "[py-complete]") {
    const auto dir = signals::test::temp_path("py-complete-examples");
    std::filesystem::create_directories(dir);
    const auto list = examples();
    REQUIRE_FALSE(list.empty());
    for (const auto& file : list) {
        INFO(file.string());
        std::ifstream in{file, std::ios::binary};
        const std::string sig{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        CHECK(script_error(sig, file.parent_path(), dir / (file.parent_path().filename().string() + "-" + file.stem().string() + ".py")) == "");
    }
}
