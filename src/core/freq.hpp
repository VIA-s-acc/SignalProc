#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "core/bank.hpp"
#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

struct FrequencyResponse {
    std::vector<double> omega;
    std::vector<std::complex<double>> h;
    std::vector<double> magnitude, magnitude_db, phase, phase_unwrapped, group_delay;
};

FrequencyResponse frequency_response(const Laurent& h, std::size_t points = 512);
std::vector<double> unwrap_phase(const std::vector<double>& p);

std::vector<double> step_response(const Laurent& h);

struct ZerosPoles {
    std::vector<std::complex<double>> zeros;
    std::ptrdiff_t poles_at_origin;
    std::vector<std::complex<double>> poles = {};
};

Result<ZerosPoles> zeros_poles(const Laurent& h);

Laurent dilate(const Laurent& h, std::size_t m);
Result<Laurent> equivalent_filter(const FilterBank& bank, std::size_t level, bool detail);
Laurent distortion_transfer(const FilterBank& bank);
Laurent alias_transfer(const FilterBank& bank);

struct BandMetrics {
    double cutoff_3db;
    double passband_ripple_db;
    double stopband_attenuation_db;
    double transition_width;
};

BandMetrics band_metrics(const FrequencyResponse& r, double passband_end, double stopband_start);

}  // namespace signals::core
