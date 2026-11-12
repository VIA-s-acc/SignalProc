#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace signals::core::dtcwt {

struct Biort {
    std::string name;
    std::vector<double> h0o, g0o, h1o, g1o;
};

struct Qshift {
    std::string name;
    std::vector<double> h0a, h0b, g0a, g0b, h1a, h1b, g1a, g1b;
};

const std::vector<Biort>& biort_table();
const std::vector<Qshift>& qshift_table();
const Biort* find_biort(std::string_view name);
const Qshift* find_qshift(std::string_view name);

struct Pyramid {
    std::vector<double> lowpass;
    std::vector<std::vector<std::complex<double>>> highpasses;
};

Result<Pyramid> forward(std::span<const double> x, std::size_t levels, const Biort& biort, const Qshift& qshift);
Result<std::vector<double>> inverse(const Pyramid& pyramid, const Biort& biort, const Qshift& qshift);

}  // namespace signals::core::dtcwt
