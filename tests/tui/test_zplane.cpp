#include <catch2/catch_test_macros.hpp>

#include <complex>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
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

TEST_CASE("the z view zooms around the pointer, pans by drag and keys, and resets", "[tui][zplane]") {
    using signals::tui::ZView;
    ZView v;
    const ftxui::Box box{10, 29, 5, 14};
    const auto before = v.at(box, 25, 7, 1.5);
    ftxui::Mouse m;
    m.button = ftxui::Mouse::WheelUp;
    m.motion = ftxui::Mouse::Pressed;
    m.x = 25;
    m.y = 7;
    REQUIRE(v.on_mouse(m, box, 1.5));
    CHECK(v.zoomed());
    CHECK(v.extent(1.5) < 1.5);
    const auto after = v.at(box, 25, 7, 1.5);
    CHECK(std::abs(after - before) < 0.1);
    const auto c = v.center();
    m.button = ftxui::Mouse::Left;
    m.x = 20;
    m.y = 10;
    REQUIRE(v.on_mouse(m, box, 1.5));
    m.motion = ftxui::Mouse::Moved;
    m.x = 22;
    REQUIRE(v.on_mouse(m, box, 1.5));
    m.motion = ftxui::Mouse::Released;
    v.on_mouse(m, box, 1.5);
    CHECK(v.center().real() < c.real());
    CHECK(v.on_key(ftxui::Event::ArrowUpCtrl, 1.5));
    CHECK(v.on_key(ftxui::Event::Character('0'), 1.5));
    CHECK_FALSE(v.zoomed());
    CHECK_FALSE(v.on_key(ftxui::Event::ArrowUp, 1.5));
}
