#pragma once

#include "core/laurent.hpp"

namespace signals::core {

int zeros_at_minus_one(const Laurent& h, double tol);
double sobolev_exponent(const Laurent& h);

}  // namespace signals::core
