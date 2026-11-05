#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/bank.hpp"
#include "core/design/complex.hpp"
#include "core/scalar.hpp"

namespace signals::core {

struct RootGroup {
    Complex<Float256> root;
    bool conjugate_pair = false;
    std::size_t reciprocal = 0;
};

struct HalfbandFactorisation {
    int p = 0;
    std::vector<RootGroup> groups;
};

HalfbandFactorisation factorise_maxflat(int p);

struct Split {
    int zeros_h0 = 0;
    std::vector<bool> group_in_h0;
};

std::vector<Split> enumerate_splits(const HalfbandFactorisation& f);
Result<FilterBank> bank_from_split(const HalfbandFactorisation& f, const Split& split, std::string name);

enum class SplitKind { orthogonal, linear_phase, other };

struct SplitInfo {
    SplitKind kind = SplitKind::other;
    double sobolev_h0 = 0.0;
    double sobolev_f0 = 0.0;
    bool wavelet = false;
};

SplitInfo classify(const FilterBank& bank);

}  // namespace signals::core
