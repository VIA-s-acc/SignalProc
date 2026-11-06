#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "tui/widgets/axis_zoom.hpp"

using namespace signals::tui;
using Catch::Approx;

TEST_CASE("zoom keeps the anchor in place and stays inside the full range", "[tui][zoom]") {
    AxisZoom z{{0, 10}};
    CHECK_FALSE(z.zoomed());
    z.zoom(2, 0.5);
    CHECK(z.axis().lo == Approx(2.5));
    CHECK(z.axis().hi == Approx(7.5));
    CHECK(z.zoomed());
    z.zoom(2, 0.25);
    CHECK(z.axis().lo == Approx(2.5));
    CHECK(z.axis().hi == Approx(5));
    z.reset();
    z.zoom(4, 0.0);
    CHECK(z.axis().lo == Approx(0));
    CHECK(z.axis().hi == Approx(2.5));
    z.zoom(0.1, 0.5);
    CHECK(z.axis().lo == Approx(0));
    CHECK(z.axis().hi == Approx(10));
    CHECK_FALSE(z.zoomed());
}

TEST_CASE("pan moves the window and stops at the edges", "[tui][zoom]") {
    AxisZoom z{{0, 10}};
    z.zoom(2, 0.5);
    z.pan(0.1);
    CHECK(z.axis().lo == Approx(3.5));
    z.pan(1);
    CHECK(z.axis().hi == Approx(10));
    CHECK(z.axis().lo == Approx(5));
    z.pan(-5);
    CHECK(z.axis().lo == Approx(0));
}

TEST_CASE("a narrow window of a pi axis is labelled in fractions of pi", "[tui][zoom]") {
    constexpr double pi = std::numbers::pi;
    AxisZoom z{{0, pi, AxisScale::pi}};
    CHECK(z.axis().scale == AxisScale::pi);
    CHECK(z.plot_x(pi / 2) == Approx(pi / 2));
    z.zoom(4, 0.5);
    CHECK(z.axis().scale == AxisScale::linear);
    CHECK(z.axis().lo == Approx(0.375));
    CHECK(z.axis().hi == Approx(0.625));
    CHECK(z.plot_x(pi / 2) == Approx(0.5));
}

TEST_CASE("a mouse column maps back to the full range", "[tui][zoom]") {
    AxisZoom z{{0, 10}};
    const ftxui::Box area{10, 29, 0, 5};
    CHECK(z.fraction_at(area, 10) == Approx(0.025));
    CHECK(z.fraction_at(area, 29) == Approx(0.975));
    z.zoom(2, 0.5);
    CHECK(z.fraction_at(area, 10) == Approx(0.2625));
    z.zoom_at(2, area, 10);
    CHECK(z.axis().lo == Approx(2.5625));
    CHECK(z.axis().hi == Approx(5.0625));
}

TEST_CASE("changing the full range keeps the visible fractions", "[tui][zoom]") {
    AxisZoom z{{0, 10}};
    z.zoom(2, 0.5);
    z.set_full({0, 20});
    CHECK(z.axis().lo == Approx(5));
    CHECK(z.axis().hi == Approx(15));
}
