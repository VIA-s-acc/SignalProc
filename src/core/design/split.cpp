#include "core/design/split.hpp"

#include <cmath>
#include <limits>

#include "core/design/maxflat.hpp"
#include "core/wavelet/regularity.hpp"

namespace signals::core {
namespace {

BasicLaurent<Float256> group_factor(const RootGroup& g) {
    if (!g.conjugate_pair) return {{Float256{1}, -g.root.re}, 0};
    return {{Float256{1}, -2 * g.root.re, g.root.re * g.root.re + g.root.im * g.root.im}, 0};
}

Laurent normalised(const BasicLaurent<Float256>& p) {
    Float256 sum{0};
    for (const auto& c : p.coeffs) sum += c;
    return convert<double>(scale(p, Float256{sqrt(Float256{2})} / sum));
}

}  // namespace

HalfbandFactorisation factorise_maxflat(int p) {
    HalfbandFactorisation f{p, {}};
    const Float256 real_tol{"1e-60"};
    for (const auto& r : maxflat_q_roots(p)) {
        if (abs(r.im) <= real_tol * (1 + modulus(r)))
            f.groups.push_back({{r.re, Float256{0}}, false, 0});
        else if (r.im > 0)
            f.groups.push_back({r, true, 0});
    }
    for (std::size_t i = 0; i < f.groups.size(); ++i) {
        const auto& r = f.groups[i].root;
        const auto m2 = r.re * r.re + r.im * r.im;
        const Complex<Float256> partner{r.re / m2, r.im / m2};
        std::size_t best = i;
        Float256 best_distance{-1};
        for (std::size_t j = 0; j < f.groups.size(); ++j) {
            if (j == i || f.groups[j].conjugate_pair != f.groups[i].conjugate_pair) continue;
            const auto d = modulus(f.groups[j].root - partner);
            if (best_distance < 0 || d < best_distance) {
                best_distance = d;
                best = j;
            }
        }
        f.groups[i].reciprocal = best;
    }
    return f;
}

std::vector<Split> enumerate_splits(const HalfbandFactorisation& f) {
    std::vector<Split> splits;
    const std::size_t groups = f.groups.size();
    for (int k = 0; k <= 2 * f.p; ++k)
        for (std::size_t mask = 0; mask < (std::size_t{1} << groups); ++mask) {
            Split s{k, std::vector<bool>(groups)};
            for (std::size_t g = 0; g < groups; ++g) s.group_in_h0[g] = ((mask >> g) & 1U) != 0;
            splits.push_back(std::move(s));
        }
    return splits;
}

Result<FilterBank> bank_from_split(const HalfbandFactorisation& f, const Split& split, std::string name) {
    if (split.zeros_h0 < 0 || split.zeros_h0 > 2 * f.p || split.group_in_h0.size() != f.groups.size())
        return make_error(Errc::invalid_argument, "bank_from_split: split does not match the factorisation");
    const BasicLaurent<Float256> one_plus{{Float256{1}, Float256{1}}, 0};
    auto h0 = power(one_plus, split.zeros_h0);
    auto f0 = power(one_plus, 2 * f.p - split.zeros_h0);
    for (std::size_t g = 0; g < f.groups.size(); ++g) {
        if (split.group_in_h0[g])
            h0 = multiply(h0, group_factor(f.groups[g]));
        else
            f0 = multiply(f0, group_factor(f.groups[g]));
    }
    return make_bank<double>(std::move(name), normalised(h0), normalised(f0));
}

SplitInfo classify(const FilterBank& bank) {
    SplitInfo info;
    if (check_orthogonality(bank, 1e-9).passed())
        info.kind = SplitKind::orthogonal;
    else if (is_linear_phase(bank.h0, 1e-9) && is_linear_phase(bank.f0, 1e-9))
        info.kind = SplitKind::linear_phase;
    info.sobolev_h0 = sobolev_exponent(bank.h0);
    info.sobolev_f0 = sobolev_exponent(bank.f0);
    info.wavelet = std::isfinite(info.sobolev_h0) && std::isfinite(info.sobolev_f0) && info.sobolev_h0 > 1e-6 &&
                   info.sobolev_f0 > 1e-6;
    return info;
}

}  // namespace signals::core
