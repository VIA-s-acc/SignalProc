#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kBank = "pipeline \"two_levels\" v2\n"
                              "\n"
                              "use \"lib/banks.sig\"\n"
                              "\n"
                              "# One level of a bank: analysis, synthesis and the sum\n"
                              "block bank_level(wavelet=haar, gain=1) {  # the level\n"
                              "    in x\n"
                              "    in noise optional\n"
                              "    out y, a, d\n"
                              "    let half = \"$gain/2\"\n"
                              "\n"
                              "    node h0  = convolve(filter=$wavelet.h0, method=direct)\n"
                              "    node h1  = convolve(filter=$wavelet.h1, method=direct)\n"
                              "    node da  = downsample(m=2, anti_alias=no, phase=1)\n"
                              "    node dd  = downsample(m=2, anti_alias=no, phase=1)\n"
                              "    node sum = add()\n"
                              "\n"
                              "    x -> h0 -> da -> sum.a\n"
                              "    x -> h1 -> dd -> sum.b\n"
                              "    da -> a\n"
                              "    dd -> d\n"
                              "    sum -> y\n"
                              "    probe sum as \"x̂\"\n"
                              "    # before the closing brace\n"
                              "}  # end of the level\n"
                              "\n"
                              "node x   = samples(values=\"4 6 10 12 8 6 5 5\", rate=8)\n"
                              "node lv1 = bank_level()\n"
                              "node lv2 = bank_level(wavelet=db2, gain=2)\n"
                              "\n"
                              "x -> lv1.x\n"
                              "lv1.a -> lv2.x\n"
                              "probe lv1.d as \"d1\"\n"
                              "probe lv2/h0 as \"h0 of the second level\"\n";

std::string with_header(const std::string& body) { return "pipeline \"t\" v2\n" + body; }

}  // namespace

TEST_CASE("a block definition reads and writes without losses", "[blocks-parse]") {
    const auto doc = parse_sig(kBank);
    INFO((doc ? std::string{} : doc.error().message + " at line " + std::to_string(doc.error().line)));
    REQUIRE(doc);
    REQUIRE(doc->uses.size() == 1);
    CHECK(doc->uses[0].path == "lib/banks.sig");
    REQUIRE(doc->blocks.size() == 1);
    const auto& b = doc->blocks[0];
    CHECK(b.name == "bank_level");
    CHECK(b.order == std::vector<std::string>{"wavelet", "gain"});
    CHECK(b.defaults.at("wavelet") == "haar");
    REQUIRE(b.ins.size() == 2);
    CHECK(b.ins[0].name == "x");
    CHECK_FALSE(b.ins[0].optional);
    CHECK(b.ins[1].optional);
    REQUIRE(b.outs.size() == 3);
    CHECK(b.outs[2].name == "d");
    CHECK(b.lets.size() == 1);
    CHECK(b.nodes.size() == 5);
    CHECK(b.edges.size() == 9);
    REQUIRE(b.probes.size() == 1);
    CHECK(b.probes[0].name == "x̂");
    CHECK(doc->probes.at(1).at.node == "lv2/h0");
    CHECK(write_sig(*doc) == kBank);
}

TEST_CASE("in and out declare ports only before a name", "[blocks-parse]") {
    const auto doc = parse_sig(with_header("block f {\n    in x\n    out y\n    node in = gain()\n    node out = gain()\n    in -> out\n    x -> in\n    out -> y\n}\n"));
    INFO((doc ? std::string{} : doc.error().message));
    REQUIRE(doc);
    const auto& b = doc->blocks.at(0);
    CHECK(b.ins.size() == 1);
    CHECK(b.outs.size() == 1);
    REQUIRE(b.edges.size() == 3);
    CHECK(b.edges[0].from.node == "in");
    CHECK(b.edges[0].to.node == "out");
}

TEST_CASE("only nodes, wires, probes, let and ports go inside a block", "[blocks-parse]") {
    for (const auto* line : {"block g {\n    }", "use \"x.sig\"", "region r = 1s..2s", "mark 0.5s \"x\""}) {
        INFO(line);
        const auto doc = parse_sig(with_header(std::string{"block f {\n    in x\n    "} + line + "\n}\n"));
        REQUIRE_FALSE(doc);
        CHECK(doc.error().line == 4);
    }
    const auto open = parse_sig(with_header("block f {\n    in x\n"));
    REQUIRE_FALSE(open);
    CHECK_THAT(open.error().message, ContainsSubstring("f"));
}

TEST_CASE("a block name is declared once and not shared with a node or a variable", "[blocks-parse]") {
    const auto twice = parse_sig(with_header("block f {\n}\nblock f {\n}\n"));
    REQUIRE_FALSE(twice);
    CHECK(twice.error().line == 4);
    const auto node = parse_sig(with_header("node f = gain()\nblock f {\n}\n"));
    REQUIRE_FALSE(node);
    CHECK_THAT(node.error().message, ContainsSubstring("f"));
    const auto var = parse_sig(with_header("let f = 2\nblock f {\n}\n"));
    REQUIRE_FALSE(var);
    CHECK_THAT(var.error().message, ContainsSubstring("f"));
}

TEST_CASE("a slash belongs to paths of the document only", "[blocks-parse]") {
    const auto node = parse_sig(with_header("node a/b = gain()\n"));
    REQUIRE_FALSE(node);
    CHECK(node.error().line == 2);
    CHECK_THAT(node.error().message, ContainsSubstring("/"));
    const auto inside = parse_sig(with_header("block f {\n    in x\n    probe x/y as \"p\"\n}\n"));
    REQUIRE_FALSE(inside);
    CHECK(inside.error().line == 4);
    const auto clash = parse_sig(with_header("block f {\n    in x\n    node x = gain()\n}\n"));
    REQUIRE_FALSE(clash);
    CHECK_THAT(clash.error().message, ContainsSubstring("x"));
}

TEST_CASE("blocks and use need the v2 header", "[blocks-parse]") {
    const auto doc = parse_sig("pipeline \"t\" v1\nblock f {\n}\n");
    REQUIRE_FALSE(doc);
    CHECK_THAT(doc.error().message, ContainsSubstring("v2"));
}

TEST_CASE("a wire cannot start or end inside a call", "[blocks-parse][review]") {
    for (const char* wires : {"c/a -> t\n", "s -> c/a\n", "s -> c -> t/g\n"}) {
        INFO(wires);
        const auto doc = parse_sig(with_header(std::string{"node s = gain()\nnode c = gain()\nnode t = gain()\n"} + wires));
        REQUIRE_FALSE(doc);
        CHECK_THAT(doc.error().message, ContainsSubstring("/"));
    }
    CHECK(parse_sig(with_header("node c = gain()\nprobe c/a as \"inner\"\n")));
}
