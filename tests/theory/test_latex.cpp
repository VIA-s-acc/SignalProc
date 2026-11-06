#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "theory/latex.hpp"
#include "theory/sections.hpp"

using namespace signals::theory;

namespace {

std::vector<std::string> math_spans(std::string_view body) {
    std::vector<std::string> out;
    bool fence = false;
    std::string text;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('\n', start);
        const auto line = body.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.starts_with("```")) fence = !fence;
        else if (!fence) {
            text += line;
            text += '\n';
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '`') {
            const auto close = text.find('`', i + 1);
            i = close == std::string::npos ? text.size() : close + 1;
            continue;
        }
        if (text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
        }
        if (text[i] != '$') {
            ++i;
            continue;
        }
        const bool display = i + 1 < text.size() && text[i + 1] == '$';
        const std::string_view delim = display ? "$$" : "$";
        std::size_t j = i + delim.size();
        while (j < text.size()) {
            if (text[j] == '\\') {
                j += 2;
                continue;
            }
            if (text.compare(j, delim.size(), delim) == 0) break;
            ++j;
        }
        if (j >= text.size()) break;
        out.push_back(text.substr(i + delim.size(), j - i - delim.size()));
        i = j + delim.size();
    }
    return out;
}

}  // namespace

TEST_CASE("every formula in the theory book parses", "[theory][latex]") {
    std::size_t count = 0;
    for (const auto& s : sections()) {
        std::string_view own = s.body;
        for (std::size_t at = 0; at < own.size();) {
            if (own[at] == '#' && (at == 0 || own[at - 1] == '\n')) {
                own = own.substr(0, at);
                break;
            }
            const auto next = own.find('\n', at);
            at = next == std::string_view::npos ? own.size() : next + 1;
        }
        for (const auto& f : math_spans(own)) {
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
