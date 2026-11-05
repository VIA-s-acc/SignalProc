#pragma once

#include <string>

namespace signals::core {

struct CheckResult {
    std::string id;
    std::string subject;
    double value = 0.0;
    double tolerance = 0.0;

    bool passed() const { return value <= tolerance; }
};

}  // namespace signals::core
