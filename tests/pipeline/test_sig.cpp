#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr std::string_view kExample = R"(pipeline "denoise" v1

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
)";

SigError error_of(std::string_view text) {
    const auto r = parse_sig(text);
    REQUIRE_FALSE(r);
    return r.error();
}

}  // namespace

TEST_CASE("the design example parses into nodes, edges and a probe", "[pipeline][sig]") {
    const auto doc = parse_sig(kExample);
    REQUIRE(doc);
    CHECK(doc->name == "denoise");
    CHECK(doc->version == 1);
    REQUIRE(doc->nodes.size() == 7);
    CHECK(doc->nodes[0].id == "src");
    CHECK(doc->nodes[0].type == "wav");
    CHECK(doc->nodes[0].params.at("path") == "test.wav");
    CHECK(doc->nodes[3].params.at("levels") == "1..4");
    CHECK(doc->nodes[5].params.at("tol") == "1e-12");
    CHECK(doc->nodes[2].line == 5);
    REQUIRE(doc->edges.size() == 7);
    CHECK(doc->edges[0].from.node == "src");
    CHECK(doc->edges[0].to.node == "ch");
    CHECK(doc->edges[4].to.node == "out");
    CHECK(doc->edges[5].to.node == "chk");
    CHECK(doc->edges[5].to.port == "ref");
    CHECK(doc->edges[6].from.node == "idwt");
    CHECK(doc->edges[6].to.port == "in");
    CHECK(doc->edges[0].from.line == 11);
    REQUIRE(doc->probes.size() == 1);
    CHECK(doc->probes[0].at.node == "dwt");
    CHECK(doc->probes[0].at.port == "d3");
    CHECK(doc->probes[0].name == "band_d3");
}

TEST_CASE("writing the design example gives it back unchanged", "[pipeline][sig]") {
    const auto doc = parse_sig(kExample);
    REQUIRE(doc);
    CHECK(write_sig(*doc) == std::string{kExample});
}

TEST_CASE("writing a parsed document is a fixed point", "[pipeline][sig]") {
    const auto doc = parse_sig("# a comment\npipeline \"x\" v1\nnode b=gain(db=-6)\n  node a = wav( path = \"in.wav\" )\na->b\nprobe b as \"g\"\n");
    REQUIRE(doc);
    const auto once = write_sig(*doc);
    const auto again = parse_sig(once);
    REQUIRE(again);
    CHECK(write_sig(*again) == once);
    CHECK(again->nodes.size() == 2);
    CHECK(again->edges.size() == 1);
    CHECK(again->probes.size() == 1);
}

TEST_CASE("syntax errors carry a line and a column", "[pipeline][sig]") {
    auto e = error_of("node a = gain(db=1\nnode b = gain()\n");
    CHECK(e.line == 1);
    CHECK(e.column == 14);
    CHECK_FALSE(e.message.empty());

    e = error_of("node a = gain()\nnode a = mixdown()\n");
    CHECK(e.line == 2);
    CHECK_THAT(e.message, ContainsSubstring("a") && ContainsSubstring("1"));

    e = error_of("node a = gain()\na ->\n");
    CHECK(e.line == 2);
    CHECK(e.column == 3);

    e = error_of("nod a = gain()\n");
    CHECK(e.line == 1);
    CHECK(e.column == 1);
    CHECK_THAT(e.message, ContainsSubstring("nod"));

    e = error_of("node a = wav(path=\"x.wav)\n");
    CHECK(e.line == 1);

    e = error_of("node a = gain(db=1, db=2)\n");
    CHECK(e.line == 1);
    CHECK_THAT(e.message, ContainsSubstring("db"));

    e = error_of("node a = wav(path=\"ёж\") x\n");
    CHECK(e.column == 25);

    e = error_of("pipeline \"x\" v2\n");
    CHECK(e.line == 1);
}

TEST_CASE("quoted strings keep spaces, Cyrillic and hashes", "[pipeline][sig]") {
    const auto doc = parse_sig("node src = wav(path=\"мои записи/тест #1.wav\")  # a comment\nnode q = wav(path=\"a\\\"b\")\nnode g = gain(db=-6)\nsrc -> g\n");
    REQUIRE(doc);
    CHECK(doc->nodes[0].params.at("path") == "мои записи/тест #1.wav");
    CHECK(doc->nodes[1].params.at("path") == "a\"b");
    CHECK(doc->nodes[2].params.at("db") == "-6");
    const auto again = parse_sig(write_sig(*doc));
    REQUIRE(again);
    CHECK(again->nodes[0].params.at("path") == "мои записи/тест #1.wav");
    CHECK(again->nodes[1].params.at("path") == "a\"b");
}

TEST_CASE("parameters may span several lines", "[pipeline][sig]") {
    const auto doc = parse_sig("node d = dwt(\n  wavelet=db2,\n  levels=6)\n");
    REQUIRE(doc);
    CHECK(doc->nodes[0].line == 1);
    CHECK(doc->nodes[0].params.at("levels") == "6");
}

TEST_CASE("set changes a parameter or names the nearest node and parameter", "[pipeline][sig]") {
    auto doc = parse_sig(kExample);
    REQUIRE(doc);
    REQUIRE(apply_set(*doc, "thr.mode=hard"));
    CHECK(doc->nodes[3].params.at("mode") == "hard");
    REQUIRE(apply_set(*doc, "out.seed=7"));
    CHECK(doc->nodes[6].params.at("seed") == "7");
    REQUIRE(apply_set(*doc, "out.path=\"my file.wav\""));
    CHECK(doc->nodes[6].params.at("path") == "my file.wav");

    auto bad = apply_set(*doc, "thx.mode=hard");
    REQUIRE_FALSE(bad);
    CHECK_THAT(bad.error().message, ContainsSubstring("thx") && ContainsSubstring("thr"));
    bad = apply_set(*doc, "thr.mdoe=hard");
    REQUIRE_FALSE(bad);
    CHECK_THAT(bad.error().message, ContainsSubstring("mdoe") && ContainsSubstring("mode"));
    bad = apply_set(*doc, "thr.mode=medium");
    REQUIRE_FALSE(bad);
    CHECK_THAT(bad.error().message, ContainsSubstring("medium"));
    CHECK_FALSE(apply_set(*doc, "thr.mode"));
    CHECK_FALSE(apply_set(*doc, "mode=hard"));
    CHECK(doc->nodes[3].params.at("mode") == "hard");
}

TEST_CASE("nearest names are suggested only when close", "[pipeline][sig]") {
    CHECK(nearest_name("thx", {"src", "thr", "out"}) == "thr");
    CHECK_FALSE(nearest_name("zzzzzz", {"src", "thr", "out"}));
}

TEST_CASE("the shipped example is canonical and every node resolves its parameters", "[pipeline][sig]") {
    std::ifstream in{std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto doc = parse_sig(text);
    REQUIRE(doc);
    CHECK(write_sig(*doc) == text);
    for (const auto& n : doc->nodes) {
        INFO(n.id);
        const auto* type = find_block_type(n.type);
        REQUIRE(type);
        CHECK(resolve_params(*type, n.params));
    }
}

TEST_CASE("bypass is read after the parentheses and written back", "[pipeline][sig]") {
    const auto doc = parse_sig("node t = threshold(mode=hard) bypass\nnode g = gain bypass\n");
    REQUIRE(doc);
    REQUIRE(doc->nodes.size() == 2);
    CHECK(doc->nodes[0].bypass);
    CHECK(doc->nodes[1].bypass);
    const auto text = write_sig(*doc);
    CHECK(text.find("threshold(mode=hard) bypass") != std::string::npos);
    CHECK(text.find("gain() bypass") != std::string::npos);
    const auto again = parse_sig(text);
    REQUIRE(again);
    CHECK(write_sig(*again) == text);
    CHECK_FALSE(parse_sig("node t = threshold() bypas\n"));
}
