#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core {

struct YinOptions {
    std::size_t frame = 2048, hop = 512;
    double fmin = 60, fmax = 1000, threshold = 0.1;
};

Result<std::vector<double>> yin_track(std::span<const double> x, double fs, const YinOptions& o, ProgressSink* progress = nullptr);
double median_f0(std::span<const double> track);

}  // namespace signals::core
