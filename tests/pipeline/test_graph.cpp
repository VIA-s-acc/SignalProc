#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

namespace {

Graph build(std::string_view text) {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    auto g = build_graph(*doc, ".");
    if (!g) FAIL(g.error().line << ":" << g.error().column << " " << g.error().message);
    return *g;
}

SigError error_of(std::string_view text) {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    auto g = build_graph(*doc, ".");
    REQUIRE_FALSE(g);
    return g.error();
}

std::vector<std::string> order_ids(const Graph& g) {
    std::vector<std::string> ids;
    for (auto i : g.order) ids.push_back(g.nodes[i].id);
    return ids;
}

const GraphNode& node(const Graph& g, std::string_view id) {
    for (const auto& n : g.nodes)
        if (n.id == id) return n;
    FAIL("no node " << id);
    return g.nodes.front();
}

}  // namespace

TEST_CASE("graph: default ports connect a chain", "[pipeline][graph]") {
    const auto g = build("node a = gen()\nnode b = gain(db=-6)\nnode c = wav_out(path=\"x.wav\")\na -> b -> c\n");
    CHECK(order_ids(g) == std::vector<std::string>{"a", "b", "c"});
    const auto& c = node(g, "c");
    REQUIRE(c.sources.size() == 1);
    REQUIRE(c.sources[0]);
    CHECK(g.nodes[c.sources[0]->node].id == "b");
    CHECK(c.sources[0]->output == 0);
    CHECK(c.params.at("bits") == "24");
    CHECK(node(g, "b").params.at("db") == "-6");
}

TEST_CASE("graph: order is topological and ties keep declaration order", "[pipeline][graph]") {
    const auto g = build("node z = mixdown()\nnode a = gen()\nnode b = gen()\nnode m = mixdown()\na -> z\nb -> m\n");
    CHECK(order_ids(g) == std::vector<std::string>{"a", "z", "b", "m"});
}

TEST_CASE("graph: a cycle is reported at the edge that closes it", "[pipeline][graph]") {
    const auto e = error_of("node a = gain()\nnode b = gain()\na -> b\nb -> a\n");
    CHECK(e.line == 4);
    CHECK_THAT(e.message, ContainsSubstring("цикл"));
    CHECK_THAT(e.message, ContainsSubstring("a → b → a"));
}

TEST_CASE("graph: a wrong port type names both types and suggests a block", "[pipeline][graph]") {
    const auto e = error_of("node s = gen()\nnode d = dwt()\nnode o = wav_out()\ns -> d -> o\n");
    CHECK(e.line == 4);
    CHECK(e.column == 11);
    CHECK_THAT(e.message, ContainsSubstring("коэффициенты"));
    CHECK_THAT(e.message, ContainsSubstring("idwt"));
}

TEST_CASE("graph: an unconnected required input is reported at its node", "[pipeline][graph]") {
    const auto e = error_of("node s = gen()\nnode c = check()\ns -> c.ref\n");
    CHECK(e.line == 2);
    CHECK_THAT(e.message, ContainsSubstring("c.in"));
}

TEST_CASE("graph: an unknown block type suggests the nearest one", "[pipeline][graph]") {
    const auto e = error_of("pipeline \"x\" v1\nnode s = gne()\n");
    CHECK(e.line == 2);
    CHECK_THAT(e.message, ContainsSubstring("gne"));
    CHECK_THAT(e.message, ContainsSubstring("gen"));
}

TEST_CASE("graph: an unknown parameter suggests the nearest one", "[pipeline][graph]") {
    const auto e = error_of("node s = gen(freg=10)\n");
    CHECK(e.line == 1);
    CHECK_THAT(e.message, ContainsSubstring("freq"));
}

TEST_CASE("graph: a bad parameter value names the node", "[pipeline][graph]") {
    const auto e = error_of("node s = gen()\nnode tone = gen(freq=abc)\n");
    CHECK(e.line == 2);
    CHECK_THAT(e.message, ContainsSubstring("tone"));
    CHECK_THAT(e.message, ContainsSubstring("freq"));
}

TEST_CASE("graph: two wires into one input name the first wire's line", "[pipeline][graph]") {
    const auto e = error_of("node a = gen()\nnode b = gen()\nnode m = mixdown()\na -> m\nb -> m\n");
    CHECK(e.line == 5);
    CHECK(e.column == 6);
    CHECK_THAT(e.message, ContainsSubstring("4"));
}

TEST_CASE("graph: an unknown node in a wire suggests the nearest one", "[pipeline][graph]") {
    const auto e = error_of("node source = gen()\nnode out = wav_out()\nsourse -> out\n");
    CHECK(e.line == 3);
    CHECK(e.column == 1);
    CHECK_THAT(e.message, ContainsSubstring("source"));
}

TEST_CASE("graph: a block with several inputs needs a port name", "[pipeline][graph]") {
    const auto e = error_of("node a = gen()\nnode c = check()\na -> c\n");
    CHECK(e.line == 3);
    CHECK_THAT(e.message, ContainsSubstring("ref"));
    CHECK_THAT(e.message, ContainsSubstring("in"));
}

TEST_CASE("graph: a wire into a source is rejected", "[pipeline][graph]") {
    const auto e = error_of("node a = gen()\nnode b = gen()\na -> b\n");
    CHECK(e.line == 3);
    CHECK_THAT(e.message, ContainsSubstring("b"));
}

TEST_CASE("graph: an unknown port lists the ports that exist", "[pipeline][graph]") {
    const auto e = error_of("node a = gen()\nnode c = check()\nnode o = wav_out()\na -> c.reff\na -> c.in\nc.out -> o\n");
    CHECK(e.line == 4);
    CHECK(e.column == 6);
    CHECK_THAT(e.message, ContainsSubstring("ref"));
}

TEST_CASE("graph: band ports of coefficients are signals", "[pipeline][graph]") {
    const auto g = build("node s = gen()\nnode d = dwt(levels=3)\nnode t = threshold()\nnode o = wav_out()\ns -> d -> t\nt.d2 -> o\n");
    const auto& o = node(g, "o");
    REQUIRE(o.sources[0]);
    CHECK(g.nodes[o.sources[0]->node].id == "t");
    CHECK(o.sources[0]->band == "d2");

    const auto e = error_of("node s = gen()\nnode d = dwt(levels=3)\nnode t = threshold()\nnode o = wav_out()\ns -> d -> t\nt.d5 -> o\n");
    CHECK(e.line == 6);
    CHECK_THAT(e.message, ContainsSubstring("d3"));
}

TEST_CASE("graph: probes resolve to wires and keep their names", "[pipeline][graph]") {
    const auto g = build("node s = gen()\nnode d = dwt(levels=2)\ns -> d\nprobe d.d1 as \"band\"\nprobe s\n");
    REQUIRE(g.probes.size() == 2);
    CHECK(g.probes[0].name == "band");
    CHECK(g.probes[0].source.band == "d1");
    CHECK(g.probes[1].name == "s");
    CHECK(g.nodes[g.probes[1].source.node].id == "s");

    const auto e = error_of("node s = gen()\ns -> x\n");
    CHECK(e.line == 2);
    const auto p = error_of("node s = gen()\nprobe s.q\n");
    CHECK(p.line == 2);
    CHECK(p.column == 7);
}

TEST_CASE("graph: the spec example builds", "[pipeline][graph]") {
    const auto g = build(R"(pipeline "denoise" v1

node src  = wav(path="test.wav")
node ch   = split(mode=lr)
node dwt  = dwt(wavelet=db2, levels=6, ext=symmetric)
node thr  = threshold(rule=bayes, mode=soft, levels=1..4, sigma=mad_d1)
node idwt = idwt(wavelet=db2, ext=symmetric)
node chk  = check(kind=pr, tol=1e-12)
node out  = wav_out(path="out.wav", bits=24, dither=tpdf)

src -> ch -> dwt -> thr -> idwt -> out
src -> chk.ref
idwt -> chk.in
probe dwt.d3 as "band_d3"
)");
    CHECK(order_ids(g) == std::vector<std::string>{"src", "ch", "dwt", "thr", "idwt", "chk", "out"});
    CHECK(g.base_dir == ".");
}
