#pragma once

#include <complex>

#include "core/error.hpp"

namespace signals::core {

double ellipk(double m);
double ellipkm1(double p);

struct Jacobi {
    double sn = 0, cn = 1, dn = 1;
};
Jacobi ellipj(double u, double m);

Result<double> ellipdeg(int n, double m1);
Result<double> arc_jac_sc1(double w, double m);

}  // namespace signals::core
