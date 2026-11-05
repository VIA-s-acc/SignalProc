#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

struct FrequencyResponse {
    std::vector<double> omega;
    std::vector<std::complex<double>> h;
    std::vector<double> magnitude, magnitude_db, phase, phase_unwrapped, group_delay;
};

FrequencyResponse frequency_response(const Laurent& h, std::size_t points = 512);

std::vector<double> step_response(const Laurent& h);

struct ZerosPoles {
    std::vector<std::complex<double>> zeros;
    std::ptrdiff_t poles_at_origin;
};

Result<ZerosPoles> zeros_poles(const Laurent& h);

}  // namespace signals::core
