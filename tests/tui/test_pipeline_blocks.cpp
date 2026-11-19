#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <map>
#include <string>

#include "i18n/i18n.hpp"
#include "pipeline/graph.hpp"
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
}
