#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "theory/sections.hpp"
#include "theory/text.hpp"

using namespace signals::theory;

TEST_CASE("the theory book splits into 132 sections", "[theory][sections]") {
    const auto& s = sections();
    CHECK(s.size() == 132);
    const auto* dwt = find_section("dwt-mallat");
    REQUIRE(dwt);
    CHECK(dwt->number == "11.1");
    CHECK(dwt->title == "DWT, пирамида Малла");
    CHECK(dwt->tier == "ядро");
    CHECK(dwt->level == 3);
    REQUIRE(dwt->parent);
    CHECK(s[*dwt->parent].id == "transforms");
    REQUIRE(find_section_by_number("5"));
    CHECK(find_section_by_number("5")->id == "haar");
    REQUIRE(find_section_by_number("0.4"));
    CHECK(find_section_by_number("0.4")->id == "conv-pywt");
    CHECK(find_section("haar-walkthrough")->body.find("x=[4,6,10,12,8,6,5,5]") != std::string::npos);
    CHECK(find_section("references")->number.empty());
    CHECK(find_section("theory")->number.empty());
    CHECK(find_section("conv-signals")->tier == "ядро");
    CHECK(find_section("toeplitz-plus-hankel")->tier == "v1");
    CHECK(find_section("nope") == nullptr);
}

TEST_CASE("ids are unique", "[theory][sections]") {
    std::set<std::string> ids;
    for (const auto& s : sections()) CHECK(ids.insert(s.id).second);
}

TEST_CASE("search folds case and finds symbols", "[theory][sections]") {
    const auto hits = search_sections("малла");
    REQUIRE_FALSE(hits.empty());
    CHECK(sections()[hits.front()].id == "dwt-mallat");
    CHECK(search_sections("МАЛЛА") == hits);
    REQUIRE_FALSE(search_sections("ещё").empty());
    CHECK(search_sections("ЕЩЕ") == search_sections("ещё"));
    CHECK(search_sections("нет такого раздела").empty());
    CHECK(search_sections("").empty());
    CHECK_FALSE(search_sections("↓2").empty());
}

TEST_CASE("display width ignores combining marks", "[theory][text]") {
    CHECK(display_width("x̂") == 1);
    CHECK(display_width("aⱼ₋₁") == 4);
    CHECK(display_width("abc") == 3);
    CHECK(fold_case("ЁЛКА Abc") == "елка abc");
}

TEST_CASE("search looks at the text a section shows, not its subsections", "[theory][sections]") {
    const auto hits = search_sections("BayesShrink");
    REQUIRE_FALSE(hits.empty());
    CHECK(sections()[hits.front()].id == "audio-denoising");
    for (auto h : hits) CHECK(sections()[h].id != "theory");
}

TEST_CASE("search finds symbols written in LaTeX", "[theory][sections]") {
    CHECK_FALSE(search_sections("P₀").empty());
    CHECK(search_sections("P₀") == search_sections("P_0"));
    CHECK_FALSE(search_sections("h₀[n]").empty());
}
