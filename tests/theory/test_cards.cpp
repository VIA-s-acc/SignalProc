#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <utility>
#include <vector>

#include "i18n/i18n.hpp"
#include "theory/cards.hpp"
#include "theory/sources.hpp"

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

TEST_CASE("every card's source file is built in and its function has a line", "[theory][sources]") {
    for (const auto language : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{language};
        for (const auto& card : signals::theory::cards()) {
            INFO(card.id << " " << card.source << " " << card.function);
            const auto text = signals::theory::embedded_source(card.source);
            REQUIRE(text);
            CHECK(text->find("#include") != std::string_view::npos);
            const auto line = signals::theory::source_line(*text, card.function);
            REQUIRE(line);
            std::size_t at = 0;
            for (std::size_t k = 0; k < *line; ++k) at = text->find('\n', at) + 1;
            const auto shown = text->substr(at, text->find('\n', at) - at);
            INFO(shown);
            CHECK(shown.find(card.function) != std::string_view::npos);
        }
    }
    CHECK_FALSE(signals::theory::embedded_source("src/no/such/file.cpp"));
}

TEST_CASE("the project headers a card source includes are built in too", "[theory][sources]") {
    const std::filesystem::path root{SIGNALS_SOURCE_DIR};
    std::size_t checked = 0;
    for (const auto& card : cards()) {
        const auto text = embedded_source(card.source);
        REQUIRE(text);
        std::istringstream in{std::string{*text}};
        for (std::string line; std::getline(in, line);) {
            const auto open = line.find("#include \"");
            if (open == std::string::npos) continue;
            const auto start = open + 10, end = line.find('"', start);
            const auto header = "src/" + line.substr(start, end - start);
            if (!std::filesystem::exists(root / header)) continue;
            INFO(card.source << " includes " << header);
            CHECK(embedded_source(header));
            ++checked;
        }
    }
    CHECK(checked > 50);
}

TEST_CASE("the C++ highlighter splits a line into kinds and carries block comments", "[theory][sources][highlight]") {
    using signals::theory::Piece;
    using Kind = signals::theory::Piece::Kind;
    bool comment = false;
    const auto kinds = [&](std::string_view line) {
        std::vector<std::pair<Kind, std::string>> out;
        for (const auto& p : signals::theory::highlight_cpp(line, comment))
            if (p.kind != Kind::plain || p.text.find_first_not_of(' ') != std::string::npos) out.emplace_back(p.kind, p.text);
        return out;
    };
    const auto line = kinds("const double x = 0.5e-3; // half");
    CHECK(line.front() == std::pair{Kind::keyword, std::string{"const"}});
    CHECK(std::find(line.begin(), line.end(), std::pair{Kind::type, std::string{"double"}}) != line.end());
    CHECK(std::find(line.begin(), line.end(), std::pair{Kind::number, std::string{"0.5e-3"}}) != line.end());
    CHECK(line.back() == std::pair{Kind::comment, std::string{"// half"}});
    const auto call = kinds("    return convolve(x, \"a // not a comment\");");
    CHECK(std::find(call.begin(), call.end(), std::pair{Kind::function, std::string{"convolve"}}) != call.end());
    CHECK(std::find(call.begin(), call.end(), std::pair{Kind::string, std::string{"\"a // not a comment\""}}) != call.end());
    CHECK(kinds("#include \"core/bank.hpp\"").front().first == Kind::preproc);
    CHECK(kinds("int a; /* opens").back().first == Kind::comment);
    CHECK(comment);
    CHECK(kinds("still inside */ int b;").front() == std::pair{Kind::comment, std::string{"still inside */"}});
    CHECK_FALSE(comment);
    std::string joined;
    for (const auto& p : signals::theory::highlight_cpp("std::vector<double> y{1, 2};", comment)) joined += p.text;
    CHECK(joined == "std::vector<double> y{1, 2};");
}

TEST_CASE("definitions are found across the built-in sources", "[theory][sources]") {
    const auto level = signals::theory::find_definition("dwt_level");
    REQUIRE(level);
    CHECK(level->path == "src/core/dwt.hpp");
    const auto bank = signals::theory::find_definition("BasicFilterBank");
    REQUIRE(bank);
    CHECK(bank->path == "src/core/bank.hpp");
    const auto text = embedded_source(bank->path);
    REQUIRE(text);
    std::size_t at = 0;
    for (std::size_t k = 0; k < bank->line; ++k) at = text->find('\n', at) + 1;
    CHECK(text->substr(at, text->find('\n', at) - at).find("struct BasicFilterBank") != std::string_view::npos);
    CHECK_FALSE(signals::theory::find_definition("return"));
    CHECK_FALSE(signals::theory::find_definition("no_such_function_anywhere"));
}

