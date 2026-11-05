#pragma once

#include <cstddef>
#include <string>

namespace signals::core {

struct CheckResult {
    std::string id;
    std::string subject;
    double value = 0.0;
    double tolerance = 0.0;

    bool passed() const { return value <= tolerance; }
};

double default_tolerance(std::size_t levels, std::size_t filter_len, double max_abs_x);

}  // namespace signals::core
