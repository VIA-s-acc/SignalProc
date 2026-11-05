#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/bank.hpp"
#include "core/dwt.hpp"
#include "core/error.hpp"
#include "core/extension.hpp"
#include "core/process/threshold.hpp"

namespace signals::core {

inline constexpr double kNormalQuartile = 0.6744897501960817;

double noise_sigma_mad(std::span<const double> finest_detail);
double universal_threshold(double sigma, std::size_t n);
double bayes_threshold(std::span<const double> detail, double sigma);

enum class ShrinkRule { visu, bayes };

struct DenoiseOptions {
    std::string wavelet = "sym8";
    std::optional<std::size_t> levels;
    Extension mode = Extension::symmetric;
    ShrinkRule rule = ShrinkRule::bayes;
    ThresholdMode threshold = ThresholdMode::soft;
    std::optional<double> sigma;
};

struct DenoiseReport {
    double sigma = 0;
    std::size_t levels = 0;
    std::vector<double> thresholds;
};

Result<std::pair<std::vector<double>, DenoiseReport>> denoise(std::span<const double> x, const DenoiseOptions& options);
Result<std::pair<AudioBuffer, std::vector<DenoiseReport>>> denoise(const AudioBuffer& audio, const DenoiseOptions& options);

Result<std::vector<double>> edit_bands(std::span<const double> x, const FilterBank& bank, std::size_t levels,
                                       const DwtOptions& options, std::span<const double> gains);

}  // namespace signals::core
