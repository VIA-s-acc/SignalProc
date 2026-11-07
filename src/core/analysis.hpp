#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

enum class CorrelationMode { full, same, valid };
Result<std::vector<double>> correlate(std::span<const double> x, std::span<const double> y, CorrelationMode mode);

std::vector<std::complex<double>> analytic_signal(std::span<const double> x);
std::vector<double> envelope(std::span<const double> x);
std::vector<double> instantaneous_frequency(std::span<const double> x, double fs);
std::vector<double> real_cepstrum(std::span<const double> x);
std::vector<double> dct2(std::span<const double> x);
std::vector<double> idct2(std::span<const double> x);

struct ComplexStft {
    std::size_t n = 0, hop = 0;
    std::vector<std::vector<std::complex<double>>> frames;
};
struct Thd {
    double f0 = 0, thd = 0, thd_db = 0;
    std::vector<double> harmonic_db;
};
Result<Thd> thd(std::span<const double> x, double fs, int harmonics = 5);
std::vector<double> onsets(std::span<const double> x, double fs, std::size_t n = 1024, std::size_t hop = 256);

Result<ComplexStft> stft_complex(std::span<const double> x, std::size_t n, std::size_t hop);
Result<std::vector<double>> istft(const ComplexStft& z);

}  // namespace signals::core
