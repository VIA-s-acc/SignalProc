#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "core/detail/pocketfft.hpp"
#include "core/metrics.hpp"

namespace signals::test {

inline double dominant_frequency(const std::vector<double>& x, double fs) {
    std::vector<std::complex<double>> spectrum(x.size() / 2 + 1);
    pocketfft::r2c({x.size()}, {sizeof(double)}, {sizeof(std::complex<double>)}, 0, pocketfft::FORWARD, x.data(),
                   spectrum.data(), 1.0);
    std::size_t best = 1;
    for (std::size_t k = 1; k < spectrum.size(); ++k)
        if (std::abs(spectrum[k]) > std::abs(spectrum[best])) best = k;
    return static_cast<double>(best) * fs / static_cast<double>(x.size());
}

inline double rms_db(const std::vector<double>& x) {
    return 10 * std::log10(core::energy(x) / static_cast<double>(x.size()));
}

}  // namespace signals::test
