#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace signals::tui {

std::string format_number(double v, int significant = 3);
std::string format_si(double v, std::string_view unit, int significant = 3);

struct Tick {
    double value;
    std::string label;
};

std::vector<Tick> nice_ticks(double lo, double hi, int max_ticks);
std::vector<Tick> log_ticks(double lo, double hi, int max_ticks);
std::vector<Tick> pi_ticks(double lo, double hi, int max_ticks);

std::string eighths_bar(double fraction, int width);

}  // namespace signals::tui
