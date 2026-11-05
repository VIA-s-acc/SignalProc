#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/laurent.hpp"

namespace signals::core {

enum class ConvMethod { automatic, direct, fft };

inline constexpr std::size_t fft_threshold = 64;

Laurent convolve(const Laurent& a, const Laurent& b, ConvMethod method = ConvMethod::automatic);

std::vector<double> circular_convolve(std::span<const double> x, const Laurent& h);

}  // namespace signals::core
