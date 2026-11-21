#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "pipeline/sig.hpp"
#include "pipeline/user_blocks.hpp"
#include "support/paths.hpp"
#include "support/timing.hpp"
#include "tui/screens/pipeline_model.hpp"

using namespace signals;
using namespace signals::tui;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {

constexpr const char* kHalve = "pipeline \"lib\" v2\n# Halves the level\nblock halve(db=-6) {\n    in x\n    out y\n    node g = gain(db=$db)\n    x -> g -> y\n}\n";
constexpr const char* kGraph = "pipeline \"g\" v2\nnode s = samples(values=\"1 2 3 4\", rate=8)\n";

fs::path fresh(const std::string& name) {
    const auto dir = test::temp_path("user-blocks") / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string read(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("a library block is copied into the graph file", "[tui][user-blocks]") {
    const auto lib = fresh("copy-lib");
    std::ofstream{lib / "halve.sig"} << kHalve;
    std::ofstream{lib / "broken.sig"} << "pipeline \"b\" v2\nblock {\n";
    const auto blocks = pipeline::load_user_blocks(lib);
    REQUIRE(blocks.size() == 1);
    PipelineModel m;
    REQUIRE(m.load_text("g", kGraph));
    const auto call = m.add_library_block(blocks.front(), blocks);
    INFO((call ? std::string{} : call.error().message));
    REQUIRE(call);
    REQUIRE(m.connect({"s", {}, 0, 0}, {*call, {}, 0, 0}));
    REQUIRE(m.doc().blocks.size() == 1);
    CHECK(m.doc().blocks.front().origin.empty());
    const auto again = m.add_library_block(blocks.front(), blocks);
    REQUIRE(again);
    REQUIRE(m.connect({"s", {}, 0, 0}, {*again, {}, 0, 0}));
    CHECK(m.doc().blocks.size() == 1);
    const auto graph = fresh("copy-graph") / "g.sig";
    REQUIRE(m.save(graph));
    fs::remove_all(lib);
    PipelineModel reopened;
    REQUIRE(reopened.load(graph));
    CHECK_FALSE(reopened.problem());
    CHECK_THAT(read(graph), ContainsSubstring("# Halves the level"));
}

TEST_CASE("a copy that differs from the library is reported and updated", "[tui][user-blocks]") {
    const auto lib = fresh("differs");
    std::ofstream{lib / "halve.sig"} << kHalve;
    PipelineModel m;
    REQUIRE(m.load_text("g", kGraph));
    REQUIRE(m.add_library_block(pipeline::load_user_blocks(lib).front(), pipeline::load_user_blocks(lib)));
    CHECK(m.differs_from(pipeline::load_user_blocks(lib)).empty());
    std::string changed = kHalve;
    changed.replace(changed.find("db=-6"), 5, "db=-9");
    std::ofstream{lib / "halve.sig"} << changed;
    const auto library = pipeline::load_user_blocks(lib);
    CHECK(m.differs_from(library) == std::vector<std::string>{"halve"});
    REQUIRE(m.update_block(library.front(), library));
    CHECK(m.doc().blocks.front().defaults.at("db") == "-9");
    CHECK(m.differs_from(library).empty());
}

TEST_CASE("saving a block writes a library file and warns about relative paths", "[tui][user-blocks]") {
    const auto lib = fresh("save");
    PipelineModel m;
    REQUIRE(m.load_text("g", "pipeline \"g\" v2\nblock inner {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n}\n"
                             "block outer {\n    out y\n    node w = wav(path=\"a.wav\")\n    node i = inner()\n    w -> i -> y\n}\nnode o = outer()\n"));
    const auto plain = m.save_block("inner", lib);
    REQUIRE(plain);
    CHECK(plain->empty());
    const auto warned = m.save_block("outer", lib);
    REQUIRE(warned);
    CHECK_FALSE(warned->empty());
    const auto blocks = pipeline::load_user_blocks(lib);
    CHECK(std::ranges::count(blocks, std::string{"outer"}, &pipeline::BlockDef::name) == 1);
    CHECK_THAT(read(lib / "outer.sig"), ContainsSubstring("block inner"));
    CHECK_FALSE(m.save_block("missing", lib));
}

TEST_CASE("library helpers stay fast on blocks that call each other", "[tui][user-blocks][review]") {
    std::string cyclic = "pipeline \"c\" v2\n";
    for (const char* name : {"b", "c"}) {
        cyclic += std::string{"block "} + name + " {\n    in x\n    out y\n";
        for (int k = 0; k < 10; ++k) cyclic += "    node n" + std::to_string(k) + " = " + (name[0] == 'b' ? "c" : "b") + "()\n";
        cyclic += "    x -> n0 -> y\n}\n";
    }
    const auto lib = fresh("cyclic");
    std::ofstream{lib / "cyclic.sig"} << cyclic;
    const auto started = std::chrono::steady_clock::now();
    PipelineModel m;
    REQUIRE(m.load_text("c", cyclic + "node s = samples(values=\"1\", rate=8)\n"));
    (void)m.save_block("b", lib / "out");
    const auto library = pipeline::load_user_blocks(lib);
    PipelineModel other;
    REQUIRE(other.load_text("g", kGraph));
    if (!library.empty()) (void)other.add_library_block(library.front(), library);
    CHECK(std::chrono::steady_clock::now() - started < test::time_budget(std::chrono::seconds{2}));
}
