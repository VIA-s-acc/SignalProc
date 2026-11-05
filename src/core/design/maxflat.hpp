#pragma once

#include <vector>

#include "core/design/complex.hpp"
#include "core/laurent.hpp"
#include "core/scalar.hpp"

namespace signals::core {

template <class T>
BasicLaurent<T> maxflat_halfband(int p) {
    const BasicLaurent<T> cos2{{T(1) / T(4), T(1) / T(2), T(1) / T(4)}, -1};
    const BasicLaurent<T> y{{T(-1) / T(4), T(1) / T(2), T(-1) / T(4)}, -1};
    BasicLaurent<T> b{{T(0)}, 0};
    BasicLaurent<T> y_power{{T(1)}, 0};
    T binomial(1);
    for (int k = 0; k < p; ++k) {
        if (k > 0) {
            binomial = binomial * T(p - 1 + k) / T(k);
            y_power = multiply(y_power, y);
        }
        b = add(b, scale(y_power, binomial));
    }
    return shift(scale(multiply(power(cos2, p), b), T(2)), 2 * p - 1);
}

std::vector<Complex<Float256>> maxflat_q_roots(int p);

}  // namespace signals::core
