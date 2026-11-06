#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <string>

#include "core/design/families.hpp"
#include "core/freq.hpp"
#include "tui/harness.hpp"
#include "tui/widgets/zplane.hpp"

using namespace signals::tui;

TEST_CASE("db2 zeros on the z-plane", "[tui][zplane]") {
    auto zp = *signals::core::zeros_poles(signals::core::wavelet("db2")->f0);
    auto t = signals::test::render_text(zplane({zp.zeros, zp.poles_at_origin}, builtin_theme("dark"), ColorDepth::truecolor, GlyphMode::unicode), 40, 20);
    CHECK(t.find("○²") != std::string::npos);
    CHECK(t.find("×³") != std::string::npos);
    CHECK(t.find("○") != t.rfind("○"));
}

TEST_CASE("points outside the field are counted", "[tui][zplane]") {
    ZPlaneSpec s{{std::complex<double>{3.7, 0.0}}, 0};
    auto t = signals::test::render_text(zplane(s, builtin_theme("dark"), ColorDepth::truecolor, GlyphMode::unicode), 40, 20);
    CHECK(t.find("вне поля: 1") != std::string::npos);
    CHECK(t.find("○") != std::string::npos);
}

TEST_CASE("ascii z-plane uses o and x", "[tui][zplane]") {
    ZPlaneSpec s{{std::complex<double>{0.5, 0.0}}, 2};
    auto t = signals::test::render_text(zplane(s, builtin_theme("dark"), ColorDepth::none, GlyphMode::ascii), 40, 20);
    CHECK(t.find('o') != std::string::npos);
    CHECK(t.find("x2") != std::string::npos);
}
