#pragma once

#include "core/laurent.hpp"

namespace signals::core {

int zeros_at_minus_one(const Laurent& h, double tol);
double sobolev_exponent(const Laurent& h);
double sobolev_exponent(const Laurent& h, int zeros);
double sobolev_from_autocorrelation(const Laurent& l_squared, int zeros);

}  // namespace signals::core
