#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "i18n/i18n.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/npz_values.hpp"
#include "pipeline/sig.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/pipeline_model.hpp"

using namespace signals;
using namespace signals::tui;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kBank = "pipeline \"bank\" v2\n"
                              "let w = haar\n"
                              "node x   = samples(values=\"4 6 10 12 8 6 5 5\", rate=8)\n"
                              "node h0  = convolve(filter=$w.h0, method=direct)\n"
                              "node h1  = convolve(filter=$w.h1, method=direct)\n"
                              "node a   = downsample(m=2, anti_alias=no, phase=1)\n"
                              "node d   = downsample(m=2, anti_alias=no, phase=1)\n"
                              "node ua  = upsample(m=2, anti_image=no)\n"
                              "node ud  = upsample(m=2, anti_image=no)\n"
                              "node f0  = convolve(filter=$w.f0, method=direct)\n"
                              "node f1  = convolve(filter=$w.f1, method=direct)\n"
                              "node sum = add()\n"
                              "x -> h0 -> a -> ua -> f0 -> sum.a\n"
                              "x -> h1 -> d -> ud -> f1 -> sum.b\n"
                              "probe a\n"
                              "probe d as \"detail\"\n"
                              "probe sum as \"x̂\"\n";

JobRunner inline_jobs() { return JobRunner{[] {}, std::chrono::milliseconds{0}, true}; }

std::map<std::string, std::vector<double>> numbers(PipelineModel& m) {
    auto jobs = inline_jobs();
    m.run(jobs);
    const auto v = m.run_view();
    INFO(v.error.value_or(""));
    REQUIRE_FALSE(v.error);
    std::map<std::string, std::vector<double>> out;
    for (const auto& [name, value] : v.probes) {
        const auto entries = pipeline::value_entries(*value);
        REQUIRE(entries);
        for (const auto& [key, array] : *entries)
            if (const auto* d = std::get_if<std::vector<double>>(&array.data)) out[name + "." + key] = *d;
    }
    return out;
}

PipelineModel bank() {
    PipelineModel m;
    REQUIRE(m.load_text("bank", kBank));
    return m;
}

}  // namespace

TEST_CASE("collapse keeps every number", "[tui][pipeline-blocks]") {
    auto m = bank();
    const auto before = numbers(m);
    REQUIRE(before.size() >= 3);
    m.marked = {"h0", "a", "ua", "f0"};
    const auto call = m.collapse("branch");
    INFO((call ? std::string{} : call.error().message));
    REQUIRE(call);
    CHECK_FALSE(m.problem());
    REQUIRE(m.doc().blocks.size() == 1);
    const auto& def = m.doc().blocks.front();
    CHECK(def.name == "branch");
    CHECK(def.nodes.size() == 4);
    CHECK(def.order == std::vector<std::string>{"w"});
    CHECK(def.ins.size() == 1);
    CHECK(def.outs.size() == 1);
    CHECK(m.marked.empty());
    const auto* g = m.graph();
    REQUIRE(g);
    CHECK(std::ranges::any_of(g->nodes, [&](const pipeline::GraphNode& n) { return n.id == *call && n.type->id == "branch"; }));
    CHECK(std::ranges::none_of(g->nodes, [](const pipeline::GraphNode& n) { return n.id.find('/') != std::string::npos; }));
    CHECK(numbers(m) == before);
    const auto text = pipeline::write_sig(m.doc());
    CHECK_THAT(text, ContainsSubstring("block branch(w=haar)"));
    CHECK_THAT(text, ContainsSubstring("probe " + *call + "/a as \"a\""));
}

TEST_CASE("expand restores the unfolded graph", "[tui][pipeline-blocks]") {
    auto m = bank();
    const auto before = numbers(m);
    m.marked = {"h1", "d", "ud", "f1"};
    const auto call = m.collapse("detail_branch");
    REQUIRE(call);
    REQUIRE(m.expand(*call));
    CHECK_FALSE(m.problem());
    for (const char* id : {"h1", "d", "ud", "f1"}) CHECK(std::ranges::any_of(m.doc().nodes, [&](const pipeline::NodeDecl& n) { return n.id == id; }));
    CHECK(std::ranges::none_of(m.doc().nodes, [&](const pipeline::NodeDecl& n) { return n.id == *call; }));
    CHECK(numbers(m) == before);
    REQUIRE_FALSE(m.expand("x"));
}

TEST_CASE("entering a block edits its definition for every call", "[tui][pipeline-blocks]") {
    auto m = bank();
    m.marked = {"h0", "a", "ua", "f0"};
    const auto call = m.collapse("branch");
    REQUIRE(call);
    const auto second = m.add_node("branch");
    REQUIRE_FALSE(second.empty());
    REQUIRE(m.connect({"x", {}, 0, 0}, {second, {}, 0, 0}));
    CHECK(m.calls_of("branch") == 2);
    REQUIRE(m.enter(*call));
    CHECK(m.inside() == *call);
    REQUIRE(m.inside_block());
    CHECK(m.inside_block()->name == "branch");
    const auto* g = m.graph();
    REQUIRE(g);
    CHECK(std::ranges::any_of(g->nodes, [](const pipeline::GraphNode& n) { return n.id == "h0"; }));
    CHECK(std::ranges::any_of(g->nodes, [](const pipeline::GraphNode& n) { return n.type->id == "in"; }));
    REQUIRE(m.set_param("h0", "method", "fft"));
    CHECK(m.doc().blocks.front().nodes.front().params.at("method") == "fft");
    m.toggle_probe({"ua", {}, 0, 0});
    CHECK(std::ranges::any_of(m.doc().probes, [&](const pipeline::ProbeDecl& p) { return p.at.node == *call + "/ua"; }));
    auto jobs = inline_jobs();
    m.run(jobs);
    const auto inner = m.run_view();
    REQUIRE(inner.nodes.contains("h0"));
    CHECK(inner.nodes.at("h0").status.state == pipeline::NodeState::done);
    CHECK_FALSE(inner.nodes.contains(*call + "/h0"));
    CHECK(m.leave());
    CHECK(m.inside().empty());
    const auto top = m.run_view();
    REQUIRE(top.nodes.contains(*call));
    CHECK(top.nodes.at(*call).status.state == pipeline::NodeState::done);
    CHECK_FALSE(m.leave());
    REQUIRE_FALSE(m.enter("x"));
}

TEST_CASE("collapse needs a fresh name and marked nodes", "[tui][pipeline-blocks]") {
    auto m = bank();
    CHECK_FALSE(m.collapse("branch"));
    m.marked = {"h0"};
    CHECK_FALSE(m.collapse("x"));
    CHECK_FALSE(m.collapse("w"));
    CHECK_FALSE(m.collapse("2bad"));
    CHECK(m.collapse("one"));
}


TEST_CASE("counting calls and unfolding stay bounded on a hostile graph", "[tui][pipeline-blocks][review]") {
    std::string wide = "pipeline \"w\" v2\nblock b0 {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n}\n";
    for (int k = 1; k <= 8; ++k) {
        wide += "block b" + std::to_string(k) + " {\n    in x\n    out y\n";
        for (int c = 0; c < 10; ++c) wide += "    node c" + std::to_string(c) + " = b" + std::to_string(k - 1) + "()\n";
        wide += "    x";
        for (int c = 0; c < 10; ++c) wide += " -> c" + std::to_string(c);
        wide += " -> y\n}\n";
    }
    wide += "node s = samples(values=\"1\", rate=8)\nnode top = b8()\ns -> top\n";
    const auto started = std::chrono::steady_clock::now();
    PipelineModel m;
    REQUIRE(m.load_text("wide", wide));
    CHECK(m.calls_of("b0") <= 100000);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{3});
    std::string doubling = "pipeline \"d\" v2\nblock big {\n    in x\n    out y\n    let a0 = \"0123456789\"\n";
    for (int k = 1; k <= 24; ++k) doubling += "    let a" + std::to_string(k) + " = \"$a" + std::to_string(k - 1) + "$a" + std::to_string(k - 1) + "\"\n";
    doubling += "    node g = gain(db=$a24)\n    x -> g -> y\n}\nnode s = samples(values=\"1\", rate=8)\nnode c = big()\ns -> c\n";
    PipelineModel d;
    REQUIRE(d.load_text("doubling", doubling));
    const auto again = std::chrono::steady_clock::now();
    CHECK_FALSE(d.expand("c"));
    CHECK(std::chrono::steady_clock::now() - again < std::chrono::seconds{2});
    std::string empty = "pipeline \"e\" v2\nblock big {\n    in x\n    out y\n    let a0 = \"\"\n";
    for (int k = 1; k <= 40; ++k) empty += "    let a" + std::to_string(k) + " = \"$a" + std::to_string(k - 1) + "$a" + std::to_string(k - 1) + "\"\n";
    empty += "    node g = gain(db=\"1$a40\")\n    x -> g -> y\n}\nnode s = samples(values=\"1\", rate=8)\nnode c = big()\ns -> c\n";
    PipelineModel e;
    REQUIRE(e.load_text("empty", empty));
    const auto third = std::chrono::steady_clock::now();
    (void)e.expand("c");
    CHECK(std::chrono::steady_clock::now() - third < std::chrono::seconds{2});
    std::string chain = "pipeline \"c\" v2\nblock big {\n    in x\n    out y\n    let a0 = 1\n";
    for (int k = 1; k <= 5000; ++k) chain += "    let a" + std::to_string(k) + " = $a" + std::to_string(k - 1) + "\n";
    chain += "    node g = gain(db=$a5000)\n    x -> g -> y\n}\nnode s = samples(values=\"1\", rate=8)\nnode c = big()\ns -> c\n";
    PipelineModel ch;
    REQUIRE(ch.load_text("chain", chain));
    (void)ch.expand("c");
    std::string deep = "pipeline \"d\" v2\nblock b0 {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n}\n";
    for (int k = 1; k <= 3000; ++k)
        deep += "block b" + std::to_string(k) + " {\n    in x\n    out y\n    node c = b" + std::to_string(k - 1) + "()\n    x -> c -> y\n}\n";
    deep += "node s = samples(values=\"1\", rate=8)\nnode top = b3000()\ns -> top\n";
    PipelineModel dp;
    REQUIRE(dp.load_text("deep", deep));
    CHECK(dp.calls_of("b0") == 0);
}

namespace {

PipelineModel loaded(const std::string& text, const std::filesystem::path& dir = {}) {
    PipelineModel m;
    if (dir.empty()) {
        REQUIRE(m.load_text("t", text));
    } else {
        std::ofstream{dir / "g.sig", std::ios::binary} << text;
        REQUIRE(m.load(dir / "g.sig"));
    }
    INFO((m.problem() ? m.problem()->message : std::string{}));
    REQUIRE_FALSE(m.problem());
    return m;
}

constexpr const char* kSrc = "node s = samples(values=\"1 2 3 4 5 6 7 8\", rate=8)\n";

}  // namespace

TEST_CASE("review 7: expand follows ports used by edges and probes inside the definition", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node g1 = gain(db=-3)\n    node g2 = gain(db=-1)\n    x -> g1 -> y\n    y -> g2\n"
                                "    probe x\n    probe y as \"out\"\n    probe g2 as \"tail\"\n}\n"} + kSrc + "node c = X()\nnode t = gain(db=-2)\ns -> c -> t\nprobe c as \"call\"\nprobe t as \"t\"\n");
    const auto before = numbers(m);
    const auto done = m.expand("c");
    INFO((done ? std::string{} : done.error().message));
    REQUIRE(done);
    CHECK_FALSE(m.problem());
    CHECK(numbers(m) == before);
}

TEST_CASE("review 7: collapse inside a block writes a file that reopens", "[tui][pipeline-blocks][review7]") {
    const auto dir = std::filesystem::temp_directory_path() / "signals-review7-reopen";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node g1 = gain(db=-3)\n    node g2 = gain(db=-1)\n    x -> g1 -> g2 -> y\n    probe g2\n}\n"} + kSrc +
                    "node c = X()\ns -> c\nprobe c as \"c\"\n");
    REQUIRE(m.enter("c"));
    m.marked = {"g2"};
    REQUIRE(m.collapse("inner"));
    REQUIRE(m.save(dir / "out.sig"));
    PipelineModel again;
    const auto ok = again.load(dir / "out.sig");
    INFO((ok ? std::string{} : ok.error().message));
    CHECK(ok);
}

TEST_CASE("review 7: expand keeps the numbers of expressions and outer names", "[tui][pipeline-blocks][review7]") {
    auto nested = loaded(std::string{"pipeline \"p\" v2\nblock inner(g=0) {\n    in x\n    out y\n    node a = gain(db=$g)\n    x -> a -> y\n}\n"
                                     "block outer(a=0) {\n    in x\n    out y\n    node c = inner(g=\"$a*2\")\n    x -> c -> y\n}\n"} + kSrc + "node o = outer(a=\"1+1\")\ns -> o\nprobe o as \"o\"\n");
    const auto first = numbers(nested);
    REQUIRE(nested.expand("o"));
    CHECK(numbers(nested) == first);
    auto scoped = loaded(std::string{"pipeline \"p\" v2\nlet b = 2\nblock X(a=0, b=0) {\n    in x\n    out y\n    node g1 = gain(db=$a)\n    node g2 = gain(db=$b)\n    x -> g1 -> g2 -> y\n}\n"} + kSrc +
                         "node c = X(a=$b, b=5)\ns -> c\nprobe c as \"c\"\n");
    const auto second = numbers(scoped);
    REQUIRE(scoped.expand("c"));
    CHECK(numbers(scoped) == second);
}

TEST_CASE("review 7: edits next to ports inside a block are applied", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node g1 = gain(db=-3)\n    node g2 = gain(db=-1)\n    x -> g1 -> g2 -> y\n}\n"} + kSrc + "node c = X()\ns -> c\n");
    REQUIRE(m.enter("c"));
    m.remove_node("g2");
    CHECK(m.doc().blocks.front().nodes.size() == 1);
    m.disconnect({{"x", {}, 0, 0}, {"g1", {}, 0, 0}});
    CHECK(m.doc().blocks.front().edges.empty());
}

TEST_CASE("review 7: a collapsed call with probes can be bypassed, deleted and unfolded", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\n"} + kSrc + "node a = gain(db=-3)\nnode b = gain(db=-1)\nnode t = gain()\ns -> a -> b -> t\nprobe b as \"b\"\nprobe t as \"t\"\n");
    m.marked = {"a", "b"};
    const auto call = m.collapse("pair");
    REQUIRE(call);
    m.toggle_probe({*call, {}, 0, 0});
    REQUIRE(m.toggle_bypass(*call));
    CHECK_FALSE(m.problem());
    REQUIRE(m.toggle_bypass(*call));
    const auto before = numbers(m);
    REQUIRE(m.expand(*call));
    CHECK(numbers(m) == before);
    m.marked = {"a", "b"};
    const auto again = m.collapse("pair2");
    REQUIRE(again);
    m.remove_node(*again);
    CHECK(std::ranges::none_of(m.doc().nodes, [&](const pipeline::NodeDecl& n) { return n.id == *again; }));
    CHECK(std::ranges::none_of(m.doc().probes, [&](const pipeline::ProbeDecl& p) { return p.at.node.starts_with(*again); }));
}

TEST_CASE("review 7: collapse refuses a selection a path leaves and re-enters", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\n"} + kSrc + "node a = gain()\nnode b = gain()\nnode c = gain()\ns -> a -> b -> c\n");
    m.marked = {"a", "c"};
    CHECK_FALSE(m.collapse("ac"));
    CHECK(m.doc().blocks.empty());
}

TEST_CASE("review 7: expand of a bypassed call is refused and ids avoid port names", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node y2 = gain()\n    x -> y2 -> y\n}\nblock W {\n    in x\n    out y\n    node c = X()\n    x -> c -> y\n}\n"} + kSrc +
                    "node w = W()\nnode b = X() bypass\ns -> w\ns -> b\n");
    CHECK_FALSE(m.expand("b"));
    auto n = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out o\n    node y = gain()\n    x -> y -> o\n}\nblock W {\n    in x\n    out y\n    node c = X()\n    x -> c -> y\n}\n"} + kSrc + "node w = W()\ns -> w\n");
    REQUIRE(n.enter("w"));
    const auto done = n.expand("c");
    INFO((done ? std::string{} : done.error().message));
    CHECK(done);
    CHECK_FALSE(n.problem());
}

TEST_CASE("review 7: a probe inside a block toggles the definition's probe", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n    probe g\n}\n"} + kSrc + "node c = X()\ns -> c\n");
    REQUIRE(m.enter("c"));
    REQUIRE(m.graph());
    CHECK_FALSE(m.graph()->probes.empty());
    m.toggle_probe({"g", {}, 0, 0});
    CHECK(m.doc().blocks.front().probes.empty());
}

TEST_CASE("review 7: unfolding a used block rebases its relative paths", "[tui][pipeline-blocks][review7]") {
    const auto dir = std::filesystem::temp_directory_path() / "signals-review7-paths";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "lib");
    std::ofstream{dir / "lib" / "lib.sig"} << "pipeline \"l\" v2\nblock src {\n    out y\n    node w = samples(values=\"1 2 3\", rate=8)\n    node c = coeffs_file(path=\"none.npz\")\n    w -> y\n}\n";
    auto m = loaded("pipeline \"p\" v2\nuse \"lib/lib.sig\"\nnode a = src()\nprobe a as \"a\"\n", dir);
    REQUIRE(m.expand("a"));
    const auto c = std::ranges::find(m.doc().nodes, std::string{"c"}, &pipeline::NodeDecl::id);
    REQUIRE(c != m.doc().nodes.end());
    CHECK(c->params.at("path") == "lib/none.npz");
}

TEST_CASE("review 7: updating from the library brings new callees", "[tui][pipeline-blocks][review7]") {
    auto m = loaded(std::string{"pipeline \"p\" v2\nblock X {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n}\n"} + kSrc + "node c = X()\ns -> c\n");
    const auto lib = pipeline::load_sig("pipeline \"l\" v2\nblock helper {\n    in x\n    out y\n    node h = gain(db=-1)\n    x -> h -> y\n}\nblock X {\n    in x\n    out y\n    node k = helper()\n    x -> k -> y\n}\n");
    REQUIRE(lib);
    const auto done = m.update_block(lib->doc.blocks.back(), lib->doc.blocks);
    INFO((done ? std::string{} : done.error().message));
    REQUIRE(done);
    CHECK_FALSE(m.problem());
    CHECK(pipeline::find_own_block(m.doc(), "helper"));
}

