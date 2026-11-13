#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::core::sst {

struct Options {
    double mu = 6.0;
    double s0 = 2.0;
    int voices = 32;
    std::size_t count = 0;
};

enum class Spacing { log, linear };

struct Bins {
    std::vector<double> freqs;
    Spacing spacing = Spacing::log;
};

using Plane = std::vector<std::vector<std::complex<double>>>;
using Row = std::function<void(std::size_t scale, std::span<const std::complex<double>> wx, std::span<const std::complex<double>> dwx)>;

struct Ridge {
    std::vector<double> freq, amp;
};

struct PowerMap {
    std::size_t rows = 0, cols = 0;
    std::vector<double> power;
};

double morlet_hat(double w, double mu);
double admissibility(double mu);
double peak_frequency(double mu);
std::vector<double> scales(const Options& o);
Options covering(double lo, double hi, int voices, double mu);
Bins log_bins(double lo, double hi, std::size_t count);

Result<void> cwt_rows(std::span<const double> x, const Options& o, const Row& row, ProgressSink* progress = nullptr);
Result<Plane> transform(std::span<const double> x, const Options& o, const Bins& bins, ProgressSink* progress = nullptr);
std::vector<double> inverse(const Plane& tx, double mu);
Result<std::vector<double>> band(std::span<const double> x, const Options& o, double lo, double hi, ProgressSink* progress = nullptr,
                                 std::size_t segment = 0);
Result<Ridge> ridge(std::span<const double> x, const Options& o, double lo, double hi, ProgressSink* progress = nullptr, std::size_t segment = 0);
Result<PowerMap> power_map(std::span<const double> x, const Options& o, const Bins& bins, std::size_t cols, ProgressSink* progress = nullptr,
                           std::size_t segment = 0);
std::size_t map_bytes(std::size_t n, const Options& o, std::size_t bins);

}  // namespace signals::core::sst
