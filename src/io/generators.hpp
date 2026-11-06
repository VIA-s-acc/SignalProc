#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/progress.hpp"

namespace signals::io {

std::vector<double> sine(double freq_hz, double amplitude, double phase, double fs, std::size_t n, const core::ProgressSink* progress = nullptr);

enum class SweepKind { linear, logarithmic };
std::vector<double> sweep(double f0, double f1, SweepKind kind, double fs, std::size_t n, const core::ProgressSink* progress = nullptr);

std::vector<double> impulse(std::size_t n, std::size_t at);
std::vector<double> step(std::size_t n, std::size_t at);

enum class NoiseColor { white, pink, brown };
std::vector<double> noise(NoiseColor color, double rms, std::size_t n, std::uint64_t seed, const core::ProgressSink* progress = nullptr);

std::vector<double> harmonics(double f0, std::span<const double> amplitudes, double fs, std::size_t n,
                              const core::ProgressSink* progress = nullptr);
std::vector<double> am(double carrier, double modulator, double depth, double fs, std::size_t n);
std::vector<double> fm(double carrier, double modulator, double index, double fs, std::size_t n);

enum class DonohoJohnstone { blocks, bumps, heavisine, doppler };
std::vector<double> donoho_johnstone(DonohoJohnstone kind, std::size_t n);

}  // namespace signals::io
