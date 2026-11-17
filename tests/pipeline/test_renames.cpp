#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <string>

#include "pipeline/renames.hpp"
#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::array<Rename, 5> kFake{
    Rename{Rename::Kind::param, "gain", "", "db", "level_db", "test"},
    Rename{Rename::Kind::choice, "check", "kind", "snr", "ratio", "test"},
    Rename{Rename::Kind::field, "check", "", "snr_db", "ratio_db", "test"},
    Rename{Rename::Kind::port, "check", "", "ref", "reference", "test"},
    Rename{Rename::Kind::block, "gain", "", "gain", "amplify", "test"},
};

PipelineDoc doc_of(std::string_view text) {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    return *doc;
}

}  // namespace

TEST_CASE("renames reach nodes, choices, ports, assert fields and block names", "[renames]") {
    auto doc = doc_of("pipeline \"r\" v1\n"
                      "node x = samples(values=\"1 2\", rate=8)\n"
                      "node g = gain(db=-6)\n"
                      "node c = check(kind=snr, tol=0)\n"
                      "node ok = assert(expr=\"a.snr_db >= 30 && a.passed && b.snr_db < 1\")\n"
                      "node other = check(kind=pr, tol=0)\n"
                      "x -> g\n"
                      "x -> c.ref\n"
                      "g -> c.in\n"
                      "c -> ok.a\n"
                      "other -> ok.b\n"
                      "x -> other.ref\n"
                      "g -> other.in\n"
                      "probe c.ref as \"r\"\n");
    const auto notes = apply_renames(doc, kFake);
    CHECK(notes.size() >= 5);
    const auto& g = doc.nodes.at(1);
    CHECK(g.type == "amplify");
    CHECK(g.params.contains("level_db"));
    CHECK_FALSE(g.params.contains("db"));
    CHECK(doc.nodes.at(2).params.at("kind") == "ratio");
    CHECK(doc.nodes.at(4).params.at("kind") == "pr");
    CHECK(doc.nodes.at(3).params.at("expr") == "a.ratio_db >= 30 && a.passed && b.ratio_db < 1");
    CHECK(doc.edges.at(1).to.port == "reference");
    CHECK(doc.edges.at(5).to.port == "reference");
    CHECK(doc.probes.at(0).at.port == "reference");
}

TEST_CASE("expression names change only where they stand", "[renames]") {
    const std::map<std::string, std::string, std::less<>> fields{{"a.snr_db", "a.ratio_db"}};
    CHECK(rename_expr_names("a.snr_db + a.snr_dbx - 2*a.snr_db", fields) == "a.ratio_db + a.snr_dbx - 2*a.ratio_db");
    CHECK(rename_expr_names("max(a.snr_db, 1)", fields) == "max(a.ratio_db, 1)");
    CHECK(rename_expr_names("not an expression ((", fields) == "not an expression ((");
}

TEST_CASE("an old name names the new one", "[renames]") {
    CHECK(renamed(kFake, Rename::Kind::param, "gain", "db") == "level_db");
    CHECK(renamed(kFake, Rename::Kind::param, "amplify", "db") == "level_db");
    CHECK(renamed(kFake, Rename::Kind::field, "check", "snr_db") == "ratio_db");
    CHECK_FALSE(renamed(kFake, Rename::Kind::param, "gain", "other"));
}
