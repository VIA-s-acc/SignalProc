#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "i18n/i18n.hpp"
#include "learn/answer.hpp"

using namespace signals::learn;

namespace {

std::vector<double> parsed(std::string_view text, const Answer& a) {
    auto v = parse_answer(text, a);
    REQUIRE(v.has_value());
    return *v;
}

}  // namespace

TEST_CASE("answers accept brackets, commas and expressions", "[learn][answer]") {
    const Answer vec{.type = AnswerType::vector, .expected = "x"};
    CHECK(parsed("1 2 3", vec) == std::vector<double>{1, 2, 3});
    CHECK(parsed("[1, 2.5, -3]", vec) == std::vector<double>{1, 2.5, -3});
    CHECK(parsed("(1; 2)", vec) == std::vector<double>{1, 2});
    const auto e = parsed("sqrt(2)/2, max(1, 2), 1 + 1", vec);
    REQUIRE(e.size() == 3);
    CHECK(e[0] == Catch::Approx(std::sqrt(2.0) / 2));
    CHECK(e[1] == 2);
    CHECK(e[2] == 2);
    const Answer num{.type = AnswerType::number, .expected = "x"};
    CHECK(parsed("0,5", num) == std::vector<double>{0.5});
    CHECK(parsed(" 1/4 + 1/4 ", num) == std::vector<double>{0.5});
    CHECK_FALSE(parse_answer("", num).has_value());
    CHECK_FALSE(parse_answer("1 + ", num).has_value());
    const Answer whole{.type = AnswerType::integer, .expected = "x"};
    CHECK(parsed("3", whole) == std::vector<double>{3});
    CHECK_FALSE(parse_answer("2.5", whole).has_value());
}

TEST_CASE("vector length mismatch is explained", "[learn][answer]") {
    signals::LanguageScope ru{signals::Language::ru};
    const Answer vec{.type = AnswerType::vector, .expected = "x"};
    const std::vector<double> given{1, 2, 3, 4, 5, 6}, expected{1, 2, 3, 4, 5};
    const auto r = compare_answer(given, expected, vec);
    CHECK_FALSE(r.passed);
    CHECK(r.detail.find("5") != std::string::npos);
    CHECK(r.detail.find("6") != std::string::npos);
}

TEST_CASE("relative and absolute tolerance", "[learn][answer]") {
    const Answer abs{.type = AnswerType::number, .expected = "x", .tol = 0.01};
    CHECK(compare_answer(std::vector<double>{1.005}, std::vector<double>{1}, abs).passed);
    CHECK_FALSE(compare_answer(std::vector<double>{1.02}, std::vector<double>{1}, abs).passed);
    const Answer rel{.type = AnswerType::number, .expected = "x", .tol = 0.02, .relative = true};
    CHECK(compare_answer(std::vector<double>{1010}, std::vector<double>{1000}, rel).passed);
    CHECK_FALSE(compare_answer(std::vector<double>{1030}, std::vector<double>{1000}, rel).passed);
    const Answer exact{.type = AnswerType::integer, .expected = "x", .tol = 0};
    CHECK(compare_answer(std::vector<double>{3}, std::vector<double>{3}, exact).passed);
    CHECK_FALSE(compare_answer(std::vector<double>{4}, std::vector<double>{3}, exact).passed);
}

TEST_CASE("choice by index or by text", "[learn][answer]") {
    const Answer c{.type = AnswerType::choice, .expected = "2", .options = {"z = 1", "z = -1", "z = 0"}};
    CHECK(parsed("2", c) == std::vector<double>{2});
    CHECK(parsed("  Z = -1 ", c) == std::vector<double>{2});
    CHECK_FALSE(parse_answer("4", c).has_value());
    CHECK_FALSE(parse_answer("z = 5", c).has_value());
    CHECK(format_answer(std::vector<double>{2}, c) == "z = -1");
}

TEST_CASE("a wrong answer does not reveal the expected value", "[learn][answer]") {
    const Answer vec{.type = AnswerType::vector, .expected = "x"};
    const auto r = compare_answer(std::vector<double>{1, 2, 7}, std::vector<double>{1, 2, 3.25}, vec);
    CHECK_FALSE(r.passed);
    CHECK(r.detail.find("3.25") == std::string::npos);
    CHECK(r.detail.find("3") != std::string::npos);
    CHECK(format_answer(std::vector<double>{1, 0.5, 1.0 / 3}, vec) == "1 0.5 0.3333333333");
}
