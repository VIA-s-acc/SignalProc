#include "core/design/daubechies.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/design/split.hpp"
#include "core/design/tables.hpp"
#include "core/metrics.hpp"

namespace signals::core {
namespace {

std::vector<std::size_t> inside_groups(const HalfbandFactorisation& f) {
    std::vector<std::size_t> inside;
    for (std::size_t g = 0; g < f.groups.size(); ++g)
        if (modulus(f.groups[g].root) < 1) inside.push_back(g);
    return inside;
}

Split orthogonal_split(const HalfbandFactorisation& f, const std::vector<std::size_t>& inside, std::size_t mask) {
    Split s{f.p, std::vector<bool>(f.groups.size(), true)};
    for (std::size_t i = 0; i < inside.size(); ++i) {
        const auto g = inside[i];
        const auto in_f0 = ((mask >> i) & 1U) != 0 ? g : f.groups[g].reciprocal;
        s.group_in_h0[in_f0] = false;
    }
    return s;
}

// Max deviation of the root part of arg F0(e^{iw}) from a line through the origin. The (1 + z^-1)^p factor
// is exactly linear and is left out: evaluating the whole filter near w = pi drowns the phase in rounding.
double phase_nonlinearity(const HalfbandFactorisation& f, const Split& split) {
    constexpr int points = 4096;
    std::vector<std::complex<double>> roots;
    for (std::size_t g = 0; g < f.groups.size(); ++g) {
        if (split.group_in_h0[g]) continue;
        const std::complex<double> r{static_cast<double>(f.groups[g].root.re), static_cast<double>(f.groups[g].root.im)};
        roots.push_back(r);
        if (f.groups[g].conjugate_pair) roots.push_back(std::conj(r));
    }
    std::vector<double> theta(points, 0.0);
    for (const auto& r : roots) {
        double previous = 0.0, offset = 0.0;
        for (int k = 0; k < points; ++k) {
            const double w = std::numbers::pi * k / (points - 1);
            double a = std::arg(1.0 - r * std::polar(1.0, -w));
            if (k > 0) {
                while (a + offset - previous > std::numbers::pi) offset -= 2 * std::numbers::pi;
                while (a + offset - previous < -std::numbers::pi) offset += 2 * std::numbers::pi;
            }
            previous = a + offset;
            theta[k] += previous;
        }
    }
    const double start = theta[0];
    for (auto& t : theta) t -= start;
    double sww = 0.0, swt = 0.0;
    for (int k = 0; k < points; ++k) {
        const double w = std::numbers::pi * k / (points - 1);
        sww += w * w;
        swt += w * theta[k];
    }
    const double slope = swt / sww;
    double worst = 0.0;
    for (int k = 0; k < points; ++k)
        worst = std::max(worst, std::abs(theta[k] - slope * std::numbers::pi * k / (points - 1)));
    return worst;
}

}  // namespace

Result<FilterBank> daubechies(int n) {
    if (n < 1 || n > 38) return make_error(Errc::unknown_wavelet, "daubechies: order must be in [1, 38]");
    const auto f = factorise_maxflat(n);
    const auto inside = inside_groups(f);
    const std::size_t all = (std::size_t{1} << inside.size()) - 1;
    return bank_from_split(f, orthogonal_split(f, inside, all), "db" + std::to_string(n));
}

Result<FilterBank> symlet(int n) {
    if (n < 2 || n > 20) return make_error(Errc::unknown_wavelet, "symlet: order must be in [2, 20]");
    const std::string name = "sym" + std::to_string(n);
    if (std::ranges::find(kSymletTableFallback, n) != kSymletTableFallback.end()) {
        const TableEntry* table = find_table(name);
        if (!table) return make_error(Errc::unknown_wavelet, "symlet: table for " + name + " is missing");
        return make_bank<double>(name, Laurent{table->dec_lo, 0}, Laurent{table->rec_lo, 0}, 1e-10);
    }
    const auto f = factorise_maxflat(n);
    const auto inside = inside_groups(f);
    std::vector<FilterBank> candidates;
    std::vector<double> scores;
    for (std::size_t mask = 0; mask < (std::size_t{1} << inside.size()); ++mask) {
        const auto split = orthogonal_split(f, inside, mask);
        auto b = bank_from_split(f, split, name);
        if (!b) return b;
        scores.push_back(phase_nonlinearity(f, split));
        candidates.push_back(std::move(*b));
    }
    const double best = *std::ranges::min_element(scores);
    const TableEntry* table = find_table(name);
    std::size_t chosen = 0;
    double chosen_distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (scores[i] > best * (1 + 1e-9) + 1e-12) continue;
        const double d = table ? max_abs_error(candidates[i].f0.coeffs, table->rec_lo) : 0.0;
        if (d < chosen_distance) {
            chosen_distance = d;
            chosen = i;
        }
    }
    return candidates[chosen];
}

}  // namespace signals::core
