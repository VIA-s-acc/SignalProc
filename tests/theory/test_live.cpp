#include <catch2/catch_test_macros.hpp>

#include <string>

#include "theory/blocks.hpp"
#include "theory/live.hpp"
#include "theory/sections.hpp"

using namespace signals::theory;

namespace {

std::string num(double d) { return d < 1e-9 ? "tiny" : std::to_string(d); }

}  // namespace

TEST_CASE("substitution formats checks and marks missing values", "[theory][live]") {
    LiveValues v;
    v.set("block.n", "450 686");
    v.set_check({"check.round_trip", "", 1.6e-15, 1e-12});
    v.set_check({"check.parseval", "", 3e-4, 1e-12});
    CHECK(substitute("N = {{block.n}}", v, Context::prose, num) == "N = 450 686");
    CHECK(substitute("{{check.round_trip}}", v, Context::prose, num) == "✓ tiny");
    CHECK(substitute("{{check.parseval}}", v, Context::prose, num).starts_with("✗ "));
    CHECK(substitute("{{check.parseval}}", v, Context::prose, num).find("порог") != std::string::npos);
    CHECK(substitute("{{ block.levels }}", v, Context::prose, num) == "—");
    CHECK(substitute("{{check.parseval}}", LiveValues{}, Context::prose, num) == "— нет данных");
    CHECK(substitute("x = {{check.round_trip}}", v, Context::math, num) == "x = \\text{tiny}");
    CHECK(substitute("a {{block.n}} b", LiveValues{}, Context::math, num) == "a \\text{—} b");
    CHECK(substitute("N = {{block.n}}", v, Context::math, num) == "N = \\text{450 686}");
    CHECK(placeholders("{{a}} и {{ b.c }} {{a}}") == std::vector<std::string>{"a", "b.c"});
}

TEST_CASE("the block catalogue has 29 core blocks with known sections", "[theory][live]") {
    std::size_t core = 0;
    for (const auto& b : block_catalog()) {
        INFO(b.id);
        CHECK(find_section(b.section) != nullptr);
        CHECK((!b.values.empty() || b.id == "reverse"));
        core += b.tier == Tier::core ? 1 : 0;
    }
    CHECK(core == 29);
    REQUIRE(find_block("dwt"));
    CHECK(find_block("dwt")->section == "dwt-mallat");
    CHECK(find_block("nope") == nullptr);
}
