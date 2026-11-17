#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("v2 header parses and keeps its version", "[sig-version]") {
    const auto doc = parse_sig("pipeline \"t\" v2\nnode x = gain()\n");
    REQUIRE(doc);
    CHECK(doc->version == 2);
    CHECK(parse_sig("pipeline \"t\" v1\nnode x = gain()\n")->version == 1);
}

TEST_CASE("a newer version is refused with an update hint", "[sig-version]") {
    const auto doc = parse_sig("pipeline \"t\" v3\nnode x = gain()\n");
    REQUIRE_FALSE(doc);
    CHECK(doc.error().line == 1);
    CHECK_THAT(doc.error().message, ContainsSubstring("v3") && ContainsSubstring("v2"));
}

TEST_CASE("v1 nodes named like keywords still parse", "[sig-version]") {
    const auto doc = parse_sig("pipeline \"t\" v1\n"
                               "node let = gain()\n"
                               "node mark = gain()\n"
                               "node block = gain()\n"
                               "node use = gain()\n"
                               "node region = gain()\n"
                               "node out = gain()\n"
                               "mark -> out\n"
                               "let.out -> block\n");
    REQUIRE(doc);
    CHECK(doc->nodes.size() == 6);
    CHECK(doc->edges.size() == 2);
    CHECK(doc->edges[0].from.node == "mark");
    CHECK(doc->edges[1].from.port == "out");
}

TEST_CASE("a v2 directive in a v1 file names the header", "[sig-version]") {
    for (const auto* line : {"let fs = 8000", "region r = 1s..2s", "block f(a=1) {", "use \"lib.sig\"", "mark 0.5s \"x\""}) {
        INFO(line);
        const auto doc = parse_sig(std::string{"pipeline \"t\" v1\n"} + line + "\n");
        REQUIRE_FALSE(doc);
        CHECK(doc.error().line == 2);
        CHECK_THAT(doc.error().message, ContainsSubstring("v2"));
    }
}

TEST_CASE("dollars are written escaped and read back", "[sig-version]") {
    CHECK(unescape_dollars("a$$b") == "a$b");
    CHECK(unescape_dollars("$$$$") == "$$");
    CHECK(unescape_dollars("plain") == "plain");
}

TEST_CASE("a v2 graph builds with single dollars", "[sig-version]") {
    const auto v2 = parse_sig("pipeline \"t\" v2\nnode x = wav(path=\"a$$b.wav\")\n");
    REQUIRE(v2);
    const auto g2 = build_graph(*v2, std::filesystem::temp_directory_path());
    REQUIRE(g2);
    CHECK(g2->nodes.at(0).params.at("path") == "a$b.wav");
    const auto v1 = parse_sig("pipeline \"t\" v1\nnode x = wav(path=\"a$$b.wav\")\n");
    REQUIRE(v1);
    const auto g1 = build_graph(*v1, std::filesystem::temp_directory_path());
    REQUIRE(g1);
    CHECK(g1->nodes.at(0).params.at("path") == "a$$b.wav");
}

TEST_CASE("a document keeps the version it was written in", "[sig-version]") {
    CHECK_THAT(write_sig(*parse_sig("pipeline \"t\" v1\nnode x = gain()\n")), ContainsSubstring("\" v1"));
    CHECK_THAT(write_sig(*parse_sig("pipeline \"t\" v2\nnode x = gain()\n")), ContainsSubstring("\" v2"));
}
