#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "i18n/i18n.hpp"
#include "theory/cards.hpp"

using namespace signals::theory;

TEST_CASE("the dwt card parses with all sections", "[theory][cards]") {
    auto c = find_card("dwt");
    REQUIRE(c);
    CHECK(c->sections.size() == 7);
    CHECK(c->source == "src/core/dwt.hpp");
    CHECK(c->function == "dwt_level");
    CHECK_FALSE(c->views.empty());
    CHECK(c->sections[0].heading == "Назначение");
}

TEST_CASE("a card without a header is an error", "[theory][cards]") {
    CHECK_FALSE(parse_card("## Назначение\nтекст"));
    CHECK_FALSE(parse_card("+++\nid = \n+++\n"));
}

TEST_CASE("the linter reports missing pieces of a broken card", "[theory][cards]") {
    auto c = parse_card("+++\nid = \"dwt\"\ntitle = \"x\"\ntheory = [\"nope\"]\nsource = \"src/none.cpp\"\nfunction = \"f\"\ntest = \"tests/none.cpp\"\n"
                        "views = [\"nowhere\"]\n+++\n## Назначение\n{{block.unknown}} $\\foo$\n");
    REQUIRE(c);
    const auto issues = lint_card(*c, SIGNALS_SOURCE_DIR);
    CHECK(issues.size() >= 6);
}

TEST_CASE("braces in implementation code are not placeholders", "[theory][cards]") {
    auto c = parse_card("+++\nid = \"dwt\"\ntitle = \"x\"\ntheory = [\"dwt-mallat\"]\nsource = \"src/core/dwt.hpp\"\nfunction = \"dwt_level\"\n"
                        "test = \"tests/core/test_dwt.cpp\"\nviews = [\"decomposition\"]\n+++\n## Реализация\n\n```cpp\nPrReport report{{a}, {b}};\n```\n");
    REQUIRE(c);
    for (const auto& i : lint_card(*c, SIGNALS_SOURCE_DIR)) CHECK(i.message.find("placeholder") == std::string::npos);
}

TEST_CASE("cards pass the linter", "[theory][cards]") {
    auto issues = lint_cards(SIGNALS_SOURCE_DIR);
    for (const auto& i : issues) UNSCOPED_INFO(i.card << ": " << i.message);
    CHECK(issues.empty());
}

TEST_CASE("card files on disk pass the linter", "[theory][cards-disk]") {
    const std::filesystem::path root = std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "theory";
    const auto language = GENERATE(signals::Language::ru, signals::Language::en);
    const signals::LanguageScope scope{language};
    const auto dir = language == signals::Language::en ? root / "en" / "blocks" : root / "blocks";
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator{dir}) {
        if (entry.path().extension() != ".md") continue;
        ++files;
        std::ifstream in{entry.path(), std::ios::binary};
        std::stringstream ss;
        ss << in.rdbuf();
        auto card = parse_card(ss.str());
        INFO(entry.path().filename().string());
        REQUIRE(card);
        CHECK(card->id + ".md" == entry.path().filename().string());
        for (const auto& i : lint_card(*card, SIGNALS_SOURCE_DIR)) {
            UNSCOPED_INFO(i.card << ": " << i.message);
            CHECK(i.message.empty());
        }
    }
    CHECK(files > 0);
}

TEST_CASE("English cards match the Russian ones and pass the linter", "[theory][cards][i18n]") {
    const auto count = cards().size();
    const signals::LanguageScope en{signals::Language::en};
    CHECK(cards().size() == count);
    const auto* c = find_card("dwt");
    REQUIRE(c);
    CHECK(c->sections.size() == 7);
    CHECK(c->sections[0].heading == "Purpose");
    CHECK(c->function == "dwt_level");
    auto issues = lint_cards(SIGNALS_SOURCE_DIR);
    for (const auto& i : issues) UNSCOPED_INFO(i.card << ": " << i.message);
    CHECK(issues.empty());
}

TEST_CASE("m7b: cards may link to every view the shell routes", "[theory][cards]") {
    const auto views = signals::theory::known_views();
    for (const char* v : {"signal", "spectrum", "resample", "filters", "designer", "diagram", "decomposition", "processing", "matrix", "explain",
                          "under_the_hood", "tutorial"}) {
        INFO(v);
        CHECK(std::find(views.begin(), views.end(), std::string_view{v}) != views.end());
    }
}
