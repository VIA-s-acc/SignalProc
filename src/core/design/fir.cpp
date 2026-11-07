#include "core/design/fir.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <Eigen/Dense>

namespace signals::core {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kPi2 = 2 * std::numbers::pi;

struct Remez {
    int r = 0;
    std::vector<double> grid, d, w, e, ad, x, y;
    std::vector<int> ext;

    void initial_guess() {
        const auto n = static_cast<int>(grid.size());
        ext.resize(static_cast<std::size_t>(r + 1));
        for (int i = 0; i <= r; ++i) ext[static_cast<std::size_t>(i)] = i * (n - 1) / r;
    }

    double calc_parms() {
        x.resize(static_cast<std::size_t>(r + 1));
        ad.resize(x.size());
        y.resize(x.size());
        for (int i = 0; i <= r; ++i) x[static_cast<std::size_t>(i)] = std::cos(kPi2 * grid[static_cast<std::size_t>(ext[static_cast<std::size_t>(i)])]);
        const int ld = (r - 1) / 15 + 1;
        for (int i = 0; i <= r; ++i) {
            double denom = 1.0;
            const double xi = x[static_cast<std::size_t>(i)];
            for (int j = 0; j < ld; ++j)
                for (int k = j; k <= r; k += ld)
                    if (k != i) denom *= 2.0 * (xi - x[static_cast<std::size_t>(k)]);
            if (std::abs(denom) < 0.00001) denom = 0.00001;
            ad[static_cast<std::size_t>(i)] = 1.0 / denom;
        }
        double numer = 0, denom = 0, sign = 1;
        for (int i = 0; i <= r; ++i) {
            const auto at = static_cast<std::size_t>(ext[static_cast<std::size_t>(i)]);
            numer += ad[static_cast<std::size_t>(i)] * d[at];
            denom += sign * ad[static_cast<std::size_t>(i)] / w[at];
            sign = -sign;
        }
        const double delta = numer / denom;
        sign = 1;
        for (int i = 0; i <= r; ++i) {
            const auto at = static_cast<std::size_t>(ext[static_cast<std::size_t>(i)]);
            y[static_cast<std::size_t>(i)] = d[at] - sign * delta / w[at];
            sign = -sign;
        }
        return delta;
    }

    double compute_a(double freq) const {
        double denom = 0, numer = 0;
        const double xc = std::cos(kPi2 * freq);
        for (int i = 0; i <= r; ++i) {
            double c = xc - x[static_cast<std::size_t>(i)];
            if (std::abs(c) < 1.0e-7) {
                numer = y[static_cast<std::size_t>(i)];
                denom = 1;
                break;
            }
            c = ad[static_cast<std::size_t>(i)] / c;
            denom += c;
            numer += c * y[static_cast<std::size_t>(i)];
        }
        return numer / denom;
    }

    void calc_error() {
        e.resize(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i) e[i] = w[i] * (d[i] - compute_a(grid[i]));
    }

    bool search() {
        const auto n = static_cast<int>(grid.size());
        std::vector<int> found;
        auto at = [&](int i) { return e[static_cast<std::size_t>(i)]; };
        if ((at(0) > 0.0 && at(0) > at(1)) || (at(0) < 0.0 && at(0) < at(1))) found.push_back(0);
        for (int i = 1; i < n - 1; ++i)
            if ((at(i) >= at(i - 1) && at(i) > at(i + 1) && at(i) > 0.0) || (at(i) <= at(i - 1) && at(i) < at(i + 1) && at(i) < 0.0)) {
                if (static_cast<int>(found.size()) >= 2 * r) return false;
                found.push_back(i);
            }
        const int j = n - 1;
        if ((at(j) > 0.0 && at(j) > at(j - 1)) || (at(j) < 0.0 && at(j) < at(j - 1))) {
            if (static_cast<int>(found.size()) >= 2 * r) return false;
            found.push_back(j);
        }
        if (static_cast<int>(found.size()) < r + 1) return false;
        auto extra = static_cast<int>(found.size()) - (r + 1);
        while (extra > 0) {
            bool up = at(found[0]) > 0.0, alt = true;
            std::size_t l = 0;
            for (std::size_t k = 1; k < found.size(); ++k) {
                if (std::abs(at(found[k])) < std::abs(at(found[l]))) l = k;
                if (up && at(found[k]) < 0.0) up = false;
                else if (!up && at(found[k]) > 0.0) up = true;
                else {
                    alt = false;
                    break;
                }
            }
            if (alt && extra == 1) l = std::abs(at(found.back())) < std::abs(at(found.front())) ? found.size() - 1 : 0;
            found.erase(found.begin() + static_cast<std::ptrdiff_t>(l));
            --extra;
        }
        for (int i = 0; i <= r; ++i) ext[static_cast<std::size_t>(i)] = found[static_cast<std::size_t>(i)];
        return true;
    }
};

}  // namespace

std::optional<RemezType> parse_remez_type(std::string_view name) {
    if (name == "bandpass") return RemezType::bandpass;
    if (name == "differentiator") return RemezType::differentiator;
    if (name == "hilbert") return RemezType::hilbert;
    return std::nullopt;
}

Result<std::vector<double>> remez(const RemezSpec& spec) {
    const int numtaps = spec.numtaps;
    auto bands = spec.bands;
    const auto numband = bands.size() / 2;
    if (numtaps < 3 || numtaps > 4096) return make_error(Errc::invalid_argument, "remez: numtaps must be in 3..4096");
    if (bands.size() % 2 != 0 || numband == 0 || spec.desired.size() != numband) return make_error(Errc::invalid_argument, "remez: need band pairs and one desired value per band");
    if (!spec.weight.empty() && spec.weight.size() != numband) return make_error(Errc::invalid_argument, "remez: need one weight per band");
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (bands[i] < 0 || bands[i] > 0.5) return make_error(Errc::invalid_argument, "remez: band edges must lie in [0, 0.5] of the sampling rate");
        if (i > 0 && bands[i] < bands[i - 1]) return make_error(Errc::invalid_argument, "remez: band edges must not decrease");
    }
    std::vector<double> weight = spec.weight.empty() ? std::vector<double>(numband, 1.0) : spec.weight;
    const bool positive = spec.type == RemezType::bandpass;
    Remez rz;
    rz.r = numtaps / 2;
    if (numtaps % 2 && positive) ++rz.r;
    const double delf = 0.5 / (spec.grid_density * rz.r);
    for (std::size_t band = 0; band < numband; ++band) {
        const double fup = bands[2 * band + 1];
        double f = bands[2 * band];
        if (band == 0 && !positive && f < delf) f = delf;
        const double desired = spec.desired[band];
        do {
            const bool diff = spec.type == RemezType::differentiator;
            rz.grid.push_back(f);
            rz.d.push_back(diff ? desired * f : desired);
            rz.w.push_back(diff && desired >= 0.0001 ? weight[band] / f : weight[band]);
            f += delf;
        } while (f <= fup);
        rz.grid.back() = fup;
    }
    if ((positive ? 0 : 1) == numtaps % 2 && rz.grid.back() > 0.5 - delf) {
        rz.grid.pop_back();
        rz.d.pop_back();
        rz.w.pop_back();
    }
    if (rz.grid.size() < static_cast<std::size_t>(rz.r + 1)) return make_error(Errc::invalid_argument, "remez: the bands are too narrow for this many taps");
    for (std::size_t i = 0; i < rz.grid.size(); ++i) {
        double c = 1;
        if (positive) {
            if (numtaps % 2 == 0) c = std::cos(kPi * rz.grid[i]);
        } else {
            c = numtaps % 2 ? std::sin(kPi2 * rz.grid[i]) : std::sin(kPi * rz.grid[i]);
        }
        if (c != 1) {
            rz.d[i] /= c;
            rz.w[i] *= c;
        }
    }
    rz.initial_guess();
    double previous = -1;
    for (int iteration = 0; iteration < spec.max_iterations; ++iteration) {
        const double dev = std::abs(rz.calc_parms());
        if (dev <= previous) {
            if (previous - dev <= 1e-9 * previous) break;
            return make_error(Errc::no_convergence, "remez: failure to converge; widen the transition bands");
        }
        previous = dev;
        rz.calc_error();
        const auto before = rz.ext;
        if (!rz.search()) return make_error(Errc::no_convergence, "remez: the extremal search failed; widen the transition bands");
        if (rz.ext == before) break;
    }
    rz.calc_parms();
    std::vector<double> taps(static_cast<std::size_t>(numtaps / 2 + 1));
    for (int i = 0; i <= numtaps / 2; ++i) {
        double c = 1;
        const double f = static_cast<double>(i) / numtaps;
        if (positive) c = numtaps % 2 ? 1 : std::cos(kPi * f);
        else c = numtaps % 2 ? std::sin(kPi2 * f) : std::sin(kPi * f);
        taps[static_cast<std::size_t>(i)] = rz.compute_a(f) * c;
    }
    std::vector<double> h(static_cast<std::size_t>(numtaps));
    const double m = (numtaps - 1.0) / 2.0;
    const int half = numtaps % 2 ? static_cast<int>(m) : numtaps / 2 - 1;
    for (int n = 0; n < numtaps; ++n) {
        const double x = kPi2 * (n - m) / numtaps;
        double val = 0;
        if (positive) {
            val = taps[0];
            for (int k = 1; k <= half; ++k) val += 2.0 * taps[static_cast<std::size_t>(k)] * std::cos(x * k);
        } else {
            if (numtaps % 2 == 0) val = taps[static_cast<std::size_t>(numtaps / 2)] * std::sin(kPi * (n - m));
            for (int k = 1; k <= half; ++k) val += 2.0 * taps[static_cast<std::size_t>(k)] * std::sin(x * k);
        }
        h[static_cast<std::size_t>(n)] = (positive ? val : -val) / numtaps;
    }
    return h;
}

Result<std::vector<double>> firls(int numtaps, std::span<const double> bands, std::span<const double> desired, std::span<const double> weight) {
    if (numtaps % 2 == 0 || numtaps < 1) return make_error(Errc::invalid_argument, "firls: numtaps must be odd and >= 1");
    if (bands.size() % 2 != 0 || bands.empty() || desired.size() != bands.size())
        return make_error(Errc::invalid_argument, "firls: need band pairs and one desired value per band edge");
    const auto nb = bands.size() / 2;
    for (std::size_t i = 0; i < bands.size(); ++i)
        if (bands[i] < 0 || bands[i] > 1) return make_error(Errc::invalid_argument, "firls: bands must lie in [0, 1] relative to Nyquist");
    for (std::size_t b = 0; b < nb; ++b) {
        if (!(bands[2 * b + 1] > bands[2 * b])) return make_error(Errc::invalid_argument, "firls: every band must have a positive width");
        if (b > 0 && bands[2 * b] < bands[2 * b - 1]) return make_error(Errc::invalid_argument, "firls: bands must not overlap");
    }
    std::vector<double> w = weight.empty() ? std::vector<double>(nb, 1.0) : std::vector<double>(weight.begin(), weight.end());
    if (w.size() != nb) return make_error(Errc::invalid_argument, "firls: need one weight per band");
    const int big_m = (numtaps - 1) / 2;
    auto sinc = [](double v) { return v == 0 ? 1.0 : std::sin(kPi * v) / (kPi * v); };
    std::vector<double> q(static_cast<std::size_t>(numtaps), 0.0);
    for (int n = 0; n < numtaps; ++n)
        for (std::size_t b = 0; b < nb; ++b) {
            const double lo = bands[2 * b], hi = bands[2 * b + 1];
            q[static_cast<std::size_t>(n)] += (sinc(hi * n) * hi - sinc(lo * n) * lo) * w[b];
        }
    Eigen::MatrixXd big_q(big_m + 1, big_m + 1);
    for (int i = 0; i <= big_m; ++i)
        for (int j = 0; j <= big_m; ++j) big_q(i, j) = q[static_cast<std::size_t>(std::abs(i - j))] + q[static_cast<std::size_t>(i + j)];
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(big_m + 1);
    for (std::size_t b = 0; b < nb; ++b) {
        const double lo = bands[2 * b], hi = bands[2 * b + 1];
        const double slope = (desired[2 * b + 1] - desired[2 * b]) / (hi - lo);
        const double c = desired[2 * b] - lo * slope;
        for (int n = 0; n <= big_m; ++n) {
            auto term = [&](double f) {
                double v = f * (slope * f + c) * sinc(f * n);
                if (n == 0) v -= slope * f * f / 2.;
                else v += slope * std::cos(n * kPi * f) / ((kPi * n) * (kPi * n));
                return v;
            };
            rhs(n) += (term(hi) - term(lo)) * w[b];
        }
    }
    Eigen::LLT<Eigen::MatrixXd> llt(big_q);
    Eigen::VectorXd a = llt.info() == Eigen::Success ? Eigen::VectorXd{llt.solve(rhs)} : Eigen::VectorXd{big_q.completeOrthogonalDecomposition().solve(rhs)};
    std::vector<double> h(static_cast<std::size_t>(numtaps));
    for (int i = 0; i < big_m; ++i) h[static_cast<std::size_t>(i)] = a(big_m - i);
    h[static_cast<std::size_t>(big_m)] = 2 * a(0);
    for (int i = 1; i <= big_m; ++i) h[static_cast<std::size_t>(big_m + i)] = a(i);
    return h;
}

}  // namespace signals::core
