#pragma once

#include <cstddef>

#include "core/laurent.hpp"

namespace signals::core {

double bessel_i0(double x);
double kaiser_beta(double attenuation_db);

struct KaiserOrder {
    std::size_t taps;
    double beta;
};
KaiserOrder kaiser_order(double attenuation_db, double transition_width);

Laurent kaiser_lowpass(std::size_t taps, double cutoff, double beta);

}  // namespace signals::core
