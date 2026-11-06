#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "theory/markdown.hpp"
#include "theory/sections.hpp"

using namespace signals::theory;

TEST_CASE("theory sections parse into blocks", "[theory][markdown]") {
    auto b = parse_markdown(find_section("haar-walkthrough")->body);
    CHECK(std::ranges::any_of(b, [](auto& x) { return x.kind == Block::Kind::numbered; }));
    auto t = parse_markdown(find_section("fbm-boundary-modes")->body);
    CHECK(std::ranges::any_of(t, [](auto& x) { return x.kind == Block::Kind::table && x.table.size() > 2; }));
    auto conv = parse_markdown(find_section("conv-pywt")->body);
    const auto table = std::ranges::find_if(conv, [](auto& x) { return x.kind == Block::Kind::table; });
    REQUIRE(table != conv.end());
    CHECK(table->table[0].size() == 3);
}

TEST_CASE("inline math, bold, code and refs", "[theory][markdown]") {
    auto b = parse_markdown("**Формулы** (§0.5): $H_0=\\tfrac1{\\sqrt2}$ и `cA[k]`, см. {#dwt-mallat}.");
    REQUIRE(b.size() == 1);
    const auto& in = b[0].inlines;
    CHECK(in[0].kind == Inline::Kind::strong);
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::ref && i.target == "conv-polyphase"; }));
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::ref && i.target == "dwt-mallat"; }));
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::math && i.text == "H_0=\\tfrac1{\\sqrt2}"; }));
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::code && i.text == "cA[k]"; }));
}

TEST_CASE("placeholders outside math become placeholder inlines", "[theory][markdown]") {
    auto b = parse_markdown("ошибка {{pr_error}} при $x = {{n}}$");
    REQUIRE(b.size() == 1);
    const auto& in = b[0].inlines;
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::placeholder && i.text == "pr_error"; }));
    CHECK(std::ranges::any_of(in, [](auto& i) { return i.kind == Inline::Kind::math && i.text == "x = {{n}}"; }));
}

TEST_CASE("lists, display math, code and rules", "[theory][markdown]") {
    auto b = parse_markdown("- один\n  - вложенный\n- два\n\n3. третий\n\n$$a=b$$\n\n```cpp\nint x;\n```\n\n---\n\n[подпись](#haar) и §9.7.");
    REQUIRE(b.size() >= 7);
    CHECK(b[0].kind == Block::Kind::bullet);
    CHECK(b[1].kind == Block::Kind::bullet);
    CHECK(b[1].level == 1);
    CHECK(b[3].kind == Block::Kind::numbered);
    CHECK(b[3].number == 3);
    CHECK(b[4].kind == Block::Kind::display_math);
    CHECK(b[4].text == "a=b");
    CHECK(b[5].kind == Block::Kind::code);
    CHECK(b[5].language == "cpp");
    CHECK(b[5].text == "int x;\n");
    CHECK(b[6].kind == Block::Kind::rule);
    const auto& last = b.back().inlines;
    CHECK(std::ranges::any_of(last, [](auto& i) { return i.kind == Inline::Kind::ref && i.target == "haar" && i.text == "подпись"; }));
    CHECK(std::ranges::any_of(last, [](auto& i) { return i.kind == Inline::Kind::ref && i.text == "§9.7"; }));
}

TEST_CASE("every § and {#id} in the book resolves", "[theory][markdown]") {
    for (const auto& s : sections()) {
        INFO(s.id);
        CHECK(unresolved_refs(s.body).empty());
    }
    CHECK(unresolved_refs("см. §99.9 и {#nope} и [x](#nope)").size() == 3);
}

TEST_CASE("math spans skip code and escaped dollars", "[theory][markdown]") {
    const auto spans = math_spans("$a$ `$b$` \\$5 $$c$$\n```\n$d$\n```\n");
    REQUIRE(spans.size() == 2);
    CHECK(spans[0] == "a");
    CHECK(spans[1] == "c");
}

TEST_CASE("a huge list number does not throw", "[theory][markdown]") {
    std::vector<Block> b;
    CHECK_NOTHROW(b = parse_markdown("2147483648. пункт\n99999999999999999999. ещё\n"));
    REQUIRE(b.size() == 2);
    CHECK(b[0].kind == Block::Kind::numbered);
}
