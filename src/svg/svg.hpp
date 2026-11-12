#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace signals::svg {

enum class Scale { linear, log10, pi };
enum class Dash { solid, dashed, dotted };

struct Axis {
    double lo = 0, hi = 1;
    Scale scale = Scale::linear;
    std::string label = {};
};

struct Series {
    std::string name;
    std::string color = "#0072B2";
    std::vector<double> x = {}, y = {};
    std::vector<double> lo = {}, hi = {};
    Dash dash = Dash::solid;
    bool stems = false;
};

struct Chart {
    std::string title;
    Axis x, y;
    std::vector<Series> series;
    std::vector<double> x_marks = {}, y_marks = {};
};

struct Heatmap {
    std::string title;
    std::size_t rows = 0, cols = 0;
    std::vector<double> values;
    double floor = 0, ceil = 1;
    Axis x = {}, y = {};
    std::function<std::string(double value)> color;
};

std::string to_svg(const Chart& chart, int width = 960, int height = 420);
std::string to_csv(const Chart& chart);
std::string to_svg(const Heatmap& map, int width = 960, int height = 420);
std::string to_csv(const Heatmap& map);
std::string hex(unsigned r, unsigned g, unsigned b);

}  // namespace signals::svg
