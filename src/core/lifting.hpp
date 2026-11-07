#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

struct LiftLevel {
    std::vector<double> s, d;
};

struct IntLiftLevel {
    std::vector<std::int64_t> s, d;
};

LiftLevel lift53(std::span<const double> x);
std::vector<double> unlift53(const LiftLevel& level);

IntLiftLevel lift53_int(std::span<const std::int64_t> x);
std::vector<std::int64_t> unlift53_int(const IntLiftLevel& level);

struct Lift97Constants {
    double alpha, beta, gamma, delta, zeta;
};
const Lift97Constants& lift97_constants();
LiftLevel lift97(std::span<const double> x);
std::vector<double> unlift97(const LiftLevel& level);

struct IntLiftPyramid {
    std::vector<std::vector<std::int64_t>> bands;
    std::vector<std::size_t> lengths;
};
Result<IntLiftPyramid> lift53_int_levels(std::span<const std::int32_t> x, std::size_t levels);
std::vector<std::int64_t> unlift53_int_levels(const IntLiftPyramid& p);

}  // namespace signals::core
