#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core {

Result<std::vector<double>> welch_psd(std::span<const double> x, double fs, std::size_t segment, std::size_t overlap,
                                      ProgressSink* progress = nullptr);
std::vector<double> welch_frequencies(double fs, std::size_t segment);

using StftFrame = std::function<void(std::size_t frame, std::span<const double> power)>;
Result<std::size_t> stft_frames(std::span<const double> x, std::size_t n, std::size_t hop, const StftFrame& out,
                                ProgressSink* progress = nullptr);
struct StftPower {
    std::size_t bins = 0, frames = 0, n = 0, hop = 0;
    std::vector<double> power;
};
Result<StftPower> stft_power(std::span<const double> x, std::size_t n, std::size_t hop, ProgressSink* progress = nullptr);

struct Peak {
    std::size_t index = 0;
    double prominence = 0;
};
std::vector<Peak> find_peaks(std::span<const double> y, double prominence = 0, std::size_t distance = 1);

struct Note {
    std::string name;
    int cents = 0;
};
Note note_of(double f);

struct Harmonic {
    int k = 0;
    double f = 0, db = 0;
};
std::optional<double> estimate_f0(std::span<const double> f, std::span<const double> db, std::span<const Peak> peaks, double limit_hz = 600);
std::vector<Harmonic> harmonics(std::span<const double> f, std::span<const double> db, double f0, int count = 8);

}  // namespace signals::core
