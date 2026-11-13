#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <numbers>
#include <string>

#include "core/expr.hpp"

using namespace signals::core;

namespace {

double num(std::string_view text) {
    auto v = eval_number(text);
    REQUIRE(v.has_value());
    return *v;
}

NameLookup names(std::map<std::string, double, std::less<>> m) {
    return [m = std::move(m)](std::string_view k) -> std::optional<double> {
        auto it = m.find(k);
        return it == m.end() ? std::nullopt : std::optional<double>{it->second};
    };
}

}  // namespace

TEST_CASE("expressions follow precedence", "[core][expr]") {
    CHECK(num("1 + 2 * 3") == 7);
    CHECK(num("(1 + 2) * 3") == 9);
    CHECK(num("2^3^2") == 512);
    CHECK(num("-2^2") == -4);
    CHECK(num("2^-10") == 1.0 / 1024);
    CHECK(num("1/3") == Catch::Approx(1.0 / 3));
    CHECK(num("10 - 4 - 3") == 3);
    CHECK(num(".5e1") == 5);
    CHECK(num("1e-12") == 1e-12);
    CHECK(std::isinf(num("1/0")));
}

TEST_CASE("comparisons and logic give 0 or 1", "[core][expr]") {
    CHECK(num("1 < 2") == 1);
    CHECK(num("2 <= 1") == 0);
    CHECK(num("3 == 3 && 2 != 3") == 1);
    CHECK(num("0 || 0") == 0);
    CHECK(num("!0") == 1);
    CHECK(num("!(1 > 0)") == 0);
    CHECK(num("1 + 1 > 1 && 2 >= 2") == 1);
    auto e = parse_expr("err <= 1e-12 && kept <= 0.1");
    REQUIRE(e.has_value());
    CHECK(eval(*e, names({{"err", 1e-13}, {"kept", 0.05}})).value() == 1);
    CHECK(eval(*e, names({{"err", 1e-11}, {"kept", 0.05}})).value() == 0);
}

TEST_CASE("functions and pi", "[core][expr]") {
    CHECK(num("sqrt(2)/2") == Catch::Approx(std::sqrt(2.0) / 2));
    CHECK(num("abs(-3)") == 3);
    CHECK(num("min(3, 1, 2)") == 1);
    CHECK(num("max(3, 1, 2)") == 3);
    CHECK(num("log10(1000)") == Catch::Approx(3));
    CHECK(num("pi/4") == Catch::Approx(std::numbers::pi / 4));
}

TEST_CASE("unknown names and syntax errors are reported with a position", "[core][expr]") {
    auto plus = parse_expr("1 +");
    REQUIRE_FALSE(plus.has_value());
    CHECK(plus.error().kind == ExprError::Kind::unexpected_end);
    CHECK(plus.error().pos == 3);

    auto stray = parse_expr("1 $ 2");
    REQUIRE_FALSE(stray.has_value());
    CHECK(stray.error().kind == ExprError::Kind::unexpected);
    CHECK(stray.error().pos == 2);
    CHECK(stray.error().token == "$");

    CHECK(parse_expr("").error().kind == ExprError::Kind::empty);
    CHECK(parse_expr("(1 + 2").error().kind == ExprError::Kind::unexpected_end);
    CHECK(parse_expr("sin(1)").error().kind == ExprError::Kind::unknown_function);
    CHECK(parse_expr("sqrt(1, 2)").error().kind == ExprError::Kind::bad_arguments);

    auto e = parse_expr("err2 < 1");
    REQUIRE(e.has_value());
    auto v = eval(*e, names({{"err", 0}}));
    REQUIRE_FALSE(v.has_value());
    CHECK(v.error().kind == ExprError::Kind::unknown_name);
    CHECK(v.error().token == "err2");
    CHECK(v.error().pos == 0);

    CHECK(eval_number("x + 1").error().kind == ExprError::Kind::unknown_name);
}

TEST_CASE("names are collected", "[core][expr]") {
    auto e = parse_expr("a.snr_db >= 30 && max(b.max_abs, err) < pi");
    REQUIRE(e.has_value());
    CHECK(expr_names(*e) == std::vector<std::string>{"a.snr_db", "b.max_abs", "err"});
}
