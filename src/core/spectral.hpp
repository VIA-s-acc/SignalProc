#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core {

Result<std::vector<double>> welch_psd(std::span<const double> x, double fs, std::size_t segment, std::size_t overlap,
                                      ProgressSink* progress = nullptr);
std::vector<double> welch_frequencies(double fs, std::size_t segment);

}  // namespace signals::core
