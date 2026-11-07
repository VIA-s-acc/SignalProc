#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core {

struct ContinuousWavelet {
    enum class Family { morl, mexh, cmor } family = Family::morl;
    double bandwidth = 1.5, center = 1.0;
    bool complex() const { return family == Family::cmor; }
    double lower() const { return -8.0; }
    double upper() const { return 8.0; }
};

Result<ContinuousWavelet> continuous_wavelet(std::string_view name);
double center_frequency(const ContinuousWavelet& w, int precision = 12);

using CwtRow = std::function<void(std::size_t scale_index, std::span<const std::complex<double>> row)>;
Result<void> cwt_rows(std::span<const double> x, std::span<const double> scales, const ContinuousWavelet& w, const CwtRow& out,
                      ProgressSink* progress = nullptr);
Result<std::vector<std::vector<std::complex<double>>>> cwt(std::span<const double> x, std::span<const double> scales,
                                                           const ContinuousWavelet& w, ProgressSink* progress = nullptr);

}  // namespace signals::core
