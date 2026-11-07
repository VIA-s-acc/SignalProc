#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core {

enum class StretchMethod { vocoder, locked, wsola };

struct StretchOptions {
    StretchMethod method = StretchMethod::vocoder;
    std::size_t n_fft = 2048, hop = 512;
};

inline constexpr double kMinStretchRate = 0.1, kMaxStretchRate = 10, kMaxShiftSemitones = 24;

std::string_view stretch_method_name(StretchMethod m);
std::optional<StretchMethod> parse_stretch_method(std::string_view name);

Result<std::vector<double>> fft_resample(std::span<const double> x, std::size_t num);
Result<std::vector<double>> time_stretch(std::span<const double> x, double rate, const StretchOptions& o, ProgressSink* progress = nullptr);
Result<std::vector<double>> pitch_shift(std::span<const double> x, double semitones, const StretchOptions& o, ProgressSink* progress = nullptr);

}  // namespace signals::core
