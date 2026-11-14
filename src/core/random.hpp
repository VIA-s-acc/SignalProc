#pragma once

#include <cstdint>
#include <optional>
#include <random>

namespace signals::core {

// std::*_distribution differs between libc++, libstdc++ and the MSVC STL; mt19937_64 itself is fixed by the standard.
// Everything here uses only that engine and IEEE +, -, *, /, sqrt, so a seed gives the same numbers on every platform.
double uniform53(std::mt19937_64& rng);
double portable_log(double x);

class NormalDraws {
public:
    explicit NormalDraws(std::uint64_t seed) : rng_{seed} {}
    double next();

private:
    std::mt19937_64 rng_;
    std::optional<double> spare_;
};

class TpdfDraws {
public:
    explicit TpdfDraws(std::uint64_t seed) : rng_{seed} {}
    double next() {
        const double a = uniform53(rng_);
        return a - uniform53(rng_);
    }

private:
    std::mt19937_64 rng_;
};

}  // namespace signals::core
