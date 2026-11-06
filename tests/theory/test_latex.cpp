#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "theory/latex.hpp"
#include "theory/math_spans.hpp"
#include "theory/sections.hpp"

using namespace signals::theory;
using signals::test::own_text;


TEST_CASE("every formula in the theory book parses", "[theory][latex]") {
    std::size_t count = 0;
    for (const auto& s : sections()) {
        for (const auto& f : math_spans(own_text(s.body))) {
            auto r = parse_latex(f);
            INFO(s.id << ": " << f << " — " << (r ? std::string{} : r.error().message));
            CHECK(r.has_value());
            ++count;
        }
    }
    CHECK(count > 1500);
}

TEST_CASE("unknown commands are reported with position", "[theory][latex]") {
    auto r = parse_latex("a + \\foo{b}");
    REQUIRE_FALSE(r);
    CHECK(r.error().offset == 4);
    CHECK(r.error().message.find("\\foo") != std::string::npos);
}

TEST_CASE("structure of a fraction, a sum with limits and a matrix", "[theory][latex]") {
    auto f = parse_latex("\\frac{1}{\\sqrt2}");
    REQUIRE(f);
    REQUIRE(f->children.size() == 1);
    CHECK(f->children[0].kind == MathNode::Kind::frac);
    CHECK(f->children[0].children[1].kind == MathNode::Kind::sqrt);
    auto s = parse_latex("\\sum_{n=0}^{N-1} x[n]");
    REQUIRE(s);
    CHECK(s->children[0].kind == MathNode::Kind::scripts);
    CHECK(s->children[0].children[0].kind == MathNode::Kind::big_op);
    CHECK(s->children[0].has_sub);
    CHECK(s->children[0].has_sup);
    auto m = parse_latex("\\begin{pmatrix}1&1\\\\-1&1\\end{pmatrix}");
    REQUIRE(m);
    CHECK(m->children[0].kind == MathNode::Kind::matrix);
    CHECK(m->children[0].columns == 2);
    CHECK(m->children[0].children.size() == 4);
    CHECK(m->children[0].left == "(");
    auto g = parse_latex("\\omega \\le \\pi");
    REQUIRE(g);
    CHECK(g->children[0].text == "ω");
    CHECK(g->children[0].ascii == "omega");
}

TEST_CASE("unbalanced braces are an error, not a crash", "[theory][latex]") {
    CHECK_FALSE(parse_latex("\\frac{a}{b"));
    CHECK_FALSE(parse_latex("a}"));
    CHECK_FALSE(parse_latex("\\left( a"));
    CHECK_FALSE(parse_latex("\\begin{pmatrix} a"));
    CHECK_FALSE(parse_latex("\\sqrt"));
    CHECK(parse_latex(""));
    CHECK_FALSE(parse_latex("x_"));
}
