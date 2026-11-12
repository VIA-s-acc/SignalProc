#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"
#include "theory/sections.hpp"
#include "theory/text.hpp"

using namespace signals::theory;

TEST_CASE("the theory book splits into 149 sections", "[theory][sections]") {
    const auto& s = sections();
    CHECK(s.size() == 150);
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

TEST_CASE("the English book has the same sections in the same order", "[theory][sections][i18n]") {
    std::vector<std::string> ru_ids;
    for (const auto& s : sections()) ru_ids.push_back(s.id);
    const signals::LanguageScope en{signals::Language::en};
    const auto& s = sections();
    REQUIRE(s.size() == ru_ids.size());
    for (std::size_t i = 0; i < s.size(); ++i) CHECK(s[i].id == ru_ids[i]);
    const auto* dwt = find_section("dwt-mallat");
    REQUIRE(dwt);
    CHECK(dwt->number == "11.1");
    CHECK(dwt->title.starts_with("DWT"));
    CHECK(dwt->tier == "core");
    CHECK(find_section("haar-walkthrough")->body.find("x=[4,6,10,12,8,6,5,5]") != std::string::npos);
}

TEST_CASE("the .sig format reference is the last chapter in both languages", "[theory][sections][sig]") {
    for (const auto lang : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{lang};
        const auto* format = find_section("sig-format");
        REQUIRE(format);
        CHECK(format->level == 2);
        for (const char* id : {"sig-lines", "sig-ports", "sig-values", "sig-checks", "sig-cli", "sig-blocks"}) {
            INFO(id);
            const auto* s = find_section(id);
            REQUIRE(s);
            REQUIRE(s->parent);
            CHECK(sections()[*s->parent].id == "sig-format");
        }
        CHECK(find_section("sig-values")->body.find("db4.f1") != std::string::npos);
    }
}

TEST_CASE("the settings and files reference is a chapter in both languages", "[theory][sections][settings]") {
    for (const auto lang : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{lang};
        const auto* settings = find_section("settings");
        REQUIRE(settings);
        CHECK(settings->level == 2);
        for (const char* id : {"settings-dir", "settings-terminal", "settings-editor", "settings-filters", "settings-env", "settings-session", "settings-outputs", "settings-reset"}) {
            INFO(id);
            const auto* s = find_section(id);
            REQUIRE(s);
            REQUIRE(s->parent);
            CHECK(sections()[*s->parent].id == "settings");
        }
        CHECK(find_section("settings-terminal")->body.find("editor") != std::string::npos);
        CHECK(find_section("settings-env")->body.find("SIGNALS_LANG") != std::string::npos);
    }
}

