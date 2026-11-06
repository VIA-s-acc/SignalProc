#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "support/golden_text.hpp"
#include "theory/cards.hpp"
#include "theory/markdown.hpp"
#include "theory/math_layout.hpp"
#include "theory/math_spans.hpp"
#include "theory/sections.hpp"
#include "theory/text.hpp"

using namespace signals::theory;
using signals::test::own_text;

namespace {

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

}  // namespace

TEST_CASE("display layouts match the goldens", "[theory][math]") {
    for (auto [name, src] : std::vector<std::pair<const char*, const char*>>{
             {"frac", "\\frac{1}{\\sqrt2}"},
             {"dwt_analysis", "a_j[k] = \\sum_n h_0[n]\\, a_{j-1}[2k+1-n]"},
             {"sum_limits", "\\sum_{n=0}^{N-1} x[n]"},
             {"pmatrix", "B=\\tfrac1{\\sqrt2}\\begin{pmatrix}1&1\\\\-1&1\\end{pmatrix}"},
             {"cases", "\\delta[k]=\\begin{cases}1,&k=0\\\\0,&k\\ne0\\end{cases}"},
             {"sqrt_tall", "\\sqrt{\\frac{\\sigma^2}{N}}"},
             {"sub_fallback", "x_{q}"},
             {"left_right", "\\left(\\frac{a}{b}\\right)^2"}}) {
        auto box = render_latex(src, MathStyle::display, MathGlyphs::unicode);
        REQUIRE(box);
        INFO(name << "\n" << join(box->lines));
        signals::test::check_golden_text(std::string{"math/"} + name, join(box->lines));
    }
}

TEST_CASE("every line of a box has the box width", "[theory][math]") {
    std::size_t formulas = 0;
    for (const auto& s : sections())
        for (const auto& f : math_spans(own_text(s.body))) {
            ++formulas;
            for (auto style : {MathStyle::display, MathStyle::inline_linear})
                for (auto glyphs : {MathGlyphs::unicode, MathGlyphs::ascii}) {
                    auto b = render_latex(f, style, glyphs);
                    REQUIRE(b);
                    INFO(s.id << ": " << f << "\n" << join(b->lines));
                    for (const auto& line : b->lines) CHECK(display_width(line) == static_cast<std::size_t>(b->width));
                    CHECK((style == MathStyle::display || b->lines.size() == 1));
                    CHECK(b->baseline >= 0);
                    CHECK(b->baseline < static_cast<int>(b->lines.size()));
                }
        }
    CHECK(formulas > 1500);
}

TEST_CASE("inline style linearises fractions", "[theory][math]") {
    CHECK(render_latex("\\frac{1+z^{-1}}{\\sqrt2}", MathStyle::inline_linear, MathGlyphs::unicode)->lines[0] == "(1 + z⁻¹)/√2");
    CHECK(render_latex("\\tfrac12(1+z^{-1})^2", MathStyle::inline_linear, MathGlyphs::unicode)->lines[0] == "½(1 + z⁻¹)²");
    CHECK(render_latex("\\sum_{n=0}^{N-1} x[n]", MathStyle::inline_linear, MathGlyphs::unicode)->lines[0] == "∑_(n=0)ᴺ⁻¹ x[n]");
}

TEST_CASE("ascii has no non-ascii bytes", "[theory][math]") {
    auto b = render_latex("\\omega\\le\\pi,\\ \\sum_{n} h_0[n]=\\sqrt2", MathStyle::display, MathGlyphs::ascii);
    REQUIRE(b);
    for (const auto& l : b->lines)
        for (unsigned char c : l) CHECK(c < 0x80);
    CHECK(render_latex("\\omega \\le \\pi", MathStyle::inline_linear, MathGlyphs::ascii)->lines[0] == "omega <= pi");
}

TEST_CASE("formulas avoid script letters that common terminal fonts lack", "[theory][math]") {
    const std::u32string missing = U"ₕₖₗₘₙₚₛₜᵦᵧᵨᵩᵪᵝᵞᵟᵠᵡ";
    const auto check = [&](const std::string& tex) {
        for (auto style : {MathStyle::display, MathStyle::inline_linear}) {
            const auto box = render_latex(tex, style, MathGlyphs::unicode);
            if (!box) continue;
            for (const auto& line : box->lines)
                for (char32_t c : to_u32(line)) {
                    INFO(tex << " -> " << line);
                    CHECK(missing.find(c) == std::u32string::npos);
                }
        }
    };
    check("f_s");
    check("\\omega_m n");
    for (const auto& s : sections())
        for (const auto& f : math_spans(own_body(s))) check(f);
    for (const auto& c : cards())
        for (const auto& part : c.sections)
            for (const auto& f : math_spans(part.body)) check(f);
}
