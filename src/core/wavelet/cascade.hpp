#pragma once

#include <vector>

#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

struct Cascade {
    std::vector<double> phi;
    double dt = 1;
    double support = 0;
    double norm_ratio = 0;
};

Result<Cascade> cascade(const Laurent& h, int iterations);

}  // namespace signals::core
