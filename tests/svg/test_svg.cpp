#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "svg/svg.hpp"

using namespace signals::svg;

namespace {

std::size_t count(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
    return n;
}

}  // namespace

TEST_CASE("a chart becomes one svg with a polyline per line, escaped text and tick labels", "[svg]") {
    Chart c;
    c.title = "a < b & c";
    c.x = {0, 3, Scale::linear, "n"};
    c.y = {-1, 1, Scale::linear, "y"};
    c.series.push_back({"line", "#0072B2", {}, {0, 1, 0, -1}});
    const auto svg = to_svg(c);
    CHECK(svg.starts_with("<svg"));
    CHECK(svg.ends_with("</svg>\n"));
    CHECK(count(svg, "<polyline") == 1);
    CHECK(svg.find("a &lt; b &amp; c") != std::string::npos);
    CHECK(svg.find(">0<") != std::string::npos);
    CHECK(svg.find(">3<") != std::string::npos);
    CHECK(svg.find(">n<") != std::string::npos);
    CHECK(svg.find("#0072B2") != std::string::npos);
}

TEST_CASE("envelopes, stems and dashes have their own svg shapes", "[svg]") {
    Chart c;
    c.x = {0, 1};
    c.y = {-1, 1};
    Series band{"band", "#D55E00"};
    band.lo = {-0.5, -0.2, -0.9};
    band.hi = {0.5, 0.4, 0.1};
    c.series.push_back(band);
    Series stems{"stems", "#009E73", {0, 0.5, 1}, {1, -1, 0.5}};
    stems.stems = true;
    c.series.push_back(stems);
    Series dashed{"dashed", "#56B4E9", {0, 1}, {0, 0}};
    dashed.dash = Dash::dashed;
    c.series.push_back(dashed);
    const auto svg = to_svg(c);
    CHECK(count(svg, "<polygon") == 1);
    CHECK(count(svg, "<circle") == 3);
    CHECK(svg.find("stroke-dasharray") != std::string::npos);
}

TEST_CASE("log and pi axes label decades and multiples of pi", "[svg]") {
    Chart log;
    log.x = {10, 10000, Scale::log10, "Hz"};
    log.y = {0, 1};
    log.series.push_back({"s", "#000000", {10, 100, 1000, 10000}, {0, 1, 0, 1}});
    const auto a = to_svg(log);
    CHECK(a.find(">100<") != std::string::npos);
    CHECK(a.find(">1000<") != std::string::npos);
    Chart pi;
    pi.x = {0, std::numbers::pi, Scale::pi, "ω"};
    pi.y = {0, 1};
    pi.series.push_back({"s", "#000000", {}, {1, 0}});
    const auto b = to_svg(pi);
    CHECK(b.find(">π/2<") != std::string::npos);
    CHECK(b.find(">π<") != std::string::npos);
}

TEST_CASE("csv is wide when the lines share x and long otherwise", "[svg]") {
    Chart same;
    same.x = {0, 2};
    same.series.push_back({"a", "#000000", {0, 1, 2}, {1, 2, 3}});
    same.series.push_back({"b", "#000000", {0, 1, 2}, {4, 5, 6}});
    CHECK(to_csv(same) == "x,a,b\n0,1,4\n1,2,5\n2,3,6\n");
    Chart spaced;
    spaced.x = {0, 2};
    spaced.series.push_back({"a", "#000000", {}, {7, 8, 9}});
    CHECK(to_csv(spaced) == "x,a\n0,7\n1,8\n2,9\n");
    Chart mixed;
    mixed.series.push_back({"a", "#000000", {0, 1}, {1, 2}});
    mixed.series.push_back({"b", "#000000", {0.5}, {3}});
    CHECK(to_csv(mixed) == "series,x,y\na,0,1\na,1,2\nb,0.5,3\n");
    Chart band;
    band.x = {0, 1};
    Series env{"e", "#000000"};
    env.lo = {-1, -2};
    env.hi = {1, 2};
    band.series.push_back(env);
    CHECK(to_csv(band) == "x,e min,e max\n0,-1,1\n1,-2,2\n");
}

TEST_CASE("a heatmap draws a rect per cell and shrinks a large one", "[svg]") {
    Heatmap h;
    h.rows = 2;
    h.cols = 3;
    h.values = {0, 1, 2, 3, 4, 5};
    h.floor = 0;
    h.ceil = 5;
    h.color = [](double t) { return t > 0.5 ? std::string{"#ffffff"} : std::string{"#000000"}; };
    const auto small = to_svg(h);
    CHECK(count(small, "<rect class=\"cell\"") == 6);
    CHECK(to_csv(h) == "row,0,1,2\n0,0,1,2\n1,3,4,5\n");
    Heatmap big;
    big.rows = 1000;
    big.cols = 2000;
    big.values.assign(big.rows * big.cols, 1.0);
    big.color = [](double) { return std::string{"#123456"}; };
    CHECK(count(to_svg(big), "<rect class=\"cell\"") <= 300u * 600u);
}
