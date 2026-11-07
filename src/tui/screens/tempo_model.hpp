#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/tempo.hpp"

namespace signals::tui {

enum class TempoMode { stretch, shift, speed };
enum class TempoSource { tone, vibrato, file };

struct TempoOptions {
    TempoMode mode = TempoMode::stretch;
    double rate = 1.25;
    double semitones = 12;
    core::StretchMethod method = core::StretchMethod::vocoder;
    TempoSource source = TempoSource::vibrato;
    friend bool operator==(const TempoOptions&, const TempoOptions&) = default;
};

struct TempoView {
    double fs = 0;
    std::vector<float> x, y;
    std::vector<double> f0_in, f0_out;
    double frame_seconds = 0, hop_seconds = 0;
    double f0_in_median = 0, f0_out_median = 0, expected = 0;
    double seconds_in = 0, seconds_out = 0;
};

inline constexpr double kTempoSeconds = 2.0, kTempoToneRate = 22050, kTempoToneHz = 220, kTempoPitchFloor = 40;
inline constexpr std::size_t kTempoWindow = 2048, kTempoHop = 512;
inline constexpr std::array<double, 14> kTempoRates{0.25, 0.33, 0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0, 3.0, 4.0};

double next_tempo_rate(double rate, bool up);
double expected_ratio(const TempoOptions& o);
double cents_off(double f, double reference);
std::vector<double> tempo_source(TempoSource s, double fs, std::size_t n);
core::Result<TempoView> compute_tempo(const TempoOptions& o, std::span<const double> x, double fs);

}  // namespace signals::tui
