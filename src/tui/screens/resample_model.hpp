#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace signals::tui {

enum class ResampleMode { down, up, rational };
enum class AntiAlias { on, bypass };
enum class ResampleSource { file, tone, sweep };

struct ResampleOptions {
    ResampleMode mode = ResampleMode::down;
    std::size_t m = 4, l = 3;
    AntiAlias filter = AntiAlias::on;
    double cutoff = 1.0;
    ResampleSource source = ResampleSource::tone;
    double tone_hz = 7000;
    friend bool operator==(const ResampleOptions&, const ResampleOptions&) = default;
};

struct ResampleView {
    double fs = 0, fs_out = 0;
    std::size_t up = 1, down = 1, taps = 0;
    std::vector<double> f_in, psd_in_db, f_out, psd_out_db;
    std::vector<std::vector<double>> copies_db;
    std::vector<double> filter_f, filter_db;
    std::vector<double> x_head, y_head;
    std::optional<double> tone_out;
    double aa_attenuation_db = 0;
    double tone_gain_db = 0, image_gain_db = 0;
};

inline constexpr std::size_t kResampleLength = 8192;
inline constexpr std::size_t kMaxFactor = 16;

double folded_frequency(double f, double fs_out);
std::vector<double> resample_source(const ResampleOptions& o, double fs, std::size_t n);
core::Result<ResampleView> compute_resample(const ResampleOptions& o, std::span<const double> x, double fs);

}  // namespace signals::tui
