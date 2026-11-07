#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

enum class RemezType { bandpass, differentiator, hilbert };

struct RemezSpec {
    int numtaps = 31;
    std::vector<double> bands;
    std::vector<double> desired;
    std::vector<double> weight = {};
    RemezType type = RemezType::bandpass;
    int grid_density = 16;
    int max_iterations = 25;
};

Result<std::vector<double>> remez(const RemezSpec& spec);
Result<std::vector<double>> firls(int numtaps, std::span<const double> bands, std::span<const double> desired, std::span<const double> weight);
std::optional<RemezType> parse_remez_type(std::string_view name);

enum class FirWindow { boxcar, hann, hamming, blackman, kaiser };
std::optional<FirWindow> parse_fir_window(std::string_view name);
std::string_view fir_window_name(FirWindow w);
std::vector<double> symmetric_window(FirWindow w, std::size_t n, double beta = 0);
Result<std::vector<double>> firwin(int numtaps, std::span<const double> cutoff, FirWindow window, bool pass_zero, double beta = 0);
Result<std::vector<double>> firwin2(int numtaps, std::span<const double> freq, std::span<const double> gain, FirWindow window, double beta = 0);

}  // namespace signals::core
