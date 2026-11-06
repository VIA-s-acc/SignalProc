#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

TEST_CASE("cards pass the linter", "[theory][cards][!mayfail]") {
    auto issues = lint_cards(SIGNALS_SOURCE_DIR);
    for (const auto& i : issues) UNSCOPED_INFO(i.card << ": " << i.message);
    CHECK(issues.empty());
}

TEST_CASE("card files on disk pass the linter", "[theory][cards-disk]") {
    const std::filesystem::path dir = std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "theory" / "blocks";
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
