#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

Result<std::vector<double>> wiener_local(std::span<const double> x, std::size_t size, std::optional<double> noise = {});

struct SpectralSubtraction {
    double alpha = 2, beta = 0.01;
    std::size_t n = 512, hop = 128;
};
Result<std::vector<double>> spectral_subtract(std::span<const double> x, std::span<const double> noise, const SpectralSubtraction& o);

Result<std::vector<double>> wiener_deconvolve(std::span<const double> y, std::span<const double> h, double snr_db);

}  // namespace signals::core
