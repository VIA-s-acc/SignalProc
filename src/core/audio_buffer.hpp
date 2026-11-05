#pragma once

#include <cstddef>
#include <vector>

namespace signals::core {

struct AudioBuffer {
    double sample_rate = 0.0;
    std::vector<std::vector<double>> channels;

    std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
};

}  // namespace signals::core
