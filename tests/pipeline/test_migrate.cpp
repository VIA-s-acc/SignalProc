#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "pipeline/migrate.hpp"
#include "pipeline/sig.hpp"

using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("v1 dollars become escaped dollars", "[migrate]") {
    const auto loaded = load_sig("pipeline \"t\" v1\nnode x = wav(path=\"a$b.wav\")\nnode g = gain()\n");
    REQUIRE(loaded);
    CHECK(loaded->from_version == 1);
    CHECK(loaded->doc.version == kSigVersion);
    CHECK(loaded->doc.nodes.at(0).params.at("path") == "a$$b.wav");
    REQUIRE(loaded->notes.size() == 1);
    CHECK_THAT(loaded->notes.front(), ContainsSubstring("x.path"));
}

TEST_CASE("v2 text loads without notes", "[migrate]") {
    const auto loaded = load_sig("pipeline \"t\" v2\nnode x = wav(path=\"a$$b.wav\")\n");
    REQUIRE(loaded);
    CHECK(loaded->from_version == 2);
    CHECK(loaded->notes.empty());
    CHECK(loaded->doc.nodes.at(0).params.at("path") == "a$$b.wav");
}

TEST_CASE("a file without a header is version 1", "[migrate]") {
    const auto loaded = load_sig("node x = wav(path=\"a$b.wav\")\n");
    REQUIRE(loaded);
    CHECK(loaded->from_version == 1);
    CHECK(loaded->doc.nodes.at(0).params.at("path") == "a$$b.wav");
}

TEST_CASE("migration keeps comments in every position", "[migrate]") {
    const std::string body = "  # header note\n"
                             "\n"
                             "# above x\n"
                             "node x = samples(values=\"1 2\", rate=8)  # tail x\n"
                             "node g = gain(\n"
                             "    # inside the parens\n"
                             "    db=-6)\n"
                             "\n"
                             "# above the wire\n"
                             "x -> g  # wire tail\n"
                             "probe g as \"g\"  # probe tail\n"
                             "# the end\n";
    const auto v1 = "# before the header\npipeline \"c\" v1" + body;
    const auto v2 = "# before the header\npipeline \"c\" v2" + body;
    const auto loaded = load_sig(v1);
    REQUIRE(loaded);
    const auto written = write_sig(loaded->doc);
    CHECK(written == write_sig(*parse_sig(v2)));
    for (const auto* comment : {"# before the header", "# header note", "# above x", "# tail x", "# inside the parens", "# above the wire", "# wire tail",
                                "# probe tail", "# the end"})
        CHECK_THAT(written, ContainsSubstring(comment));
    CHECK_THAT(written, ContainsSubstring("pipeline \"c\" v2"));
}

TEST_CASE("migration steps chain up to the current version", "[migrate]") {
    const auto steps = migration_steps();
    REQUIRE_FALSE(steps.empty());
    int version = 1;
    for (const auto& s : steps) {
        CHECK(s.from == version);
        CHECK(s.to == version + 1);
        version = s.to;
    }
    CHECK(version == kSigVersion);
}
