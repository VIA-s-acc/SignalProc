#include "core/design/fir.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

#include <Eigen/Dense>

#include "core/design/window_fir.hpp"
#include "core/detail/pocketfft.hpp"

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
        if (spec.type == RemezType::differentiator) {
            rz.d.back() = desired * fup;
            rz.w.back() = desired >= 0.0001 ? weight[band] / fup : weight[band];
        }
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

std::optional<FirWindow> parse_fir_window(std::string_view name) {
    if (name == "boxcar" || name == "rect") return FirWindow::boxcar;
    if (name == "hann") return FirWindow::hann;
    if (name == "hamming") return FirWindow::hamming;
    if (name == "blackman") return FirWindow::blackman;
    if (name == "kaiser") return FirWindow::kaiser;
    return std::nullopt;
}

std::string_view fir_window_name(FirWindow w) {
    switch (w) {
    case FirWindow::boxcar: return "boxcar";
    case FirWindow::hann: return "hann";
    case FirWindow::hamming: return "hamming";
    case FirWindow::blackman: return "blackman";
    case FirWindow::kaiser: return "kaiser";
    }
    return "boxcar";
}

std::vector<double> symmetric_window(FirWindow w, std::size_t n, double beta) {
    std::vector<double> out(n, 1.0);
    if (n < 2) return out;
    const double last = static_cast<double>(n - 1);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = 2 * kPi * static_cast<double>(k) / last;
        switch (w) {
        case FirWindow::boxcar: break;
        case FirWindow::hann: out[k] = 0.5 - 0.5 * std::cos(t); break;
        case FirWindow::hamming: out[k] = 0.54 - 0.46 * std::cos(t); break;
        case FirWindow::blackman: out[k] = 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2 * t); break;
        case FirWindow::kaiser: {
            const double r = 2 * static_cast<double>(k) / last - 1;
            out[k] = bessel_i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel_i0(beta);
            break;
        }
        }
    }
    return out;
}

Result<std::vector<double>> firwin(int numtaps, std::span<const double> cutoff, FirWindow window, bool pass_zero, double beta) {
    if (numtaps < 1) return make_error(Errc::invalid_argument, "firwin: numtaps must be at least 1");
    if (cutoff.empty()) return make_error(Errc::invalid_argument, "firwin: at least one cutoff is needed");
    for (std::size_t i = 0; i < cutoff.size(); ++i) {
        if (!(cutoff[i] > 0 && cutoff[i] < 1)) return make_error(Errc::invalid_argument, "firwin: cutoffs must lie strictly between 0 and 1 (Nyquist)");
        if (i && !(cutoff[i] > cutoff[i - 1])) return make_error(Errc::invalid_argument, "firwin: cutoffs must increase");
    }
    const bool pass_nyquist = (cutoff.size() % 2 == 1) != pass_zero;
    if (pass_nyquist && numtaps % 2 == 0)
        return make_error(Errc::invalid_argument, "firwin: a filter that passes Nyquist needs an odd number of taps");
    std::vector<double> edges;
    if (pass_zero) edges.push_back(0.0);
    edges.insert(edges.end(), cutoff.begin(), cutoff.end());
    if (pass_nyquist) edges.push_back(1.0);
    const auto n = static_cast<std::size_t>(numtaps);
    const double alpha = 0.5 * (numtaps - 1);
    auto sinc = [](double x) { return x == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x); };
    std::vector<double> h(n, 0.0);
    for (std::size_t b = 0; b + 1 < edges.size(); b += 2) {
        const double left = edges[b], right = edges[b + 1];
        for (std::size_t k = 0; k < n; ++k) {
            const double m = static_cast<double>(k) - alpha;
            h[k] += right * sinc(right * m) - left * sinc(left * m);
        }
    }
    const auto w = symmetric_window(window, n, beta);
    for (std::size_t k = 0; k < n; ++k) h[k] *= w[k];
    const double left = edges[0], right = edges[1];
    const double scale = left == 0 ? 0.0 : right == 1 ? 1.0 : 0.5 * (left + right);
    double s = 0;
    for (std::size_t k = 0; k < n; ++k) s += h[k] * std::cos(kPi * (static_cast<double>(k) - alpha) * scale);
    for (auto& v : h) v /= s;
    return h;
}

Result<std::vector<double>> firwin2(int numtaps, std::span<const double> freq_in, std::span<const double> gain, FirWindow window, double beta) {
    if (numtaps < 3) return make_error(Errc::invalid_argument, "firwin2: numtaps must be at least 3");
    if (freq_in.size() != gain.size() || freq_in.size() < 2) return make_error(Errc::invalid_argument, "firwin2: freq and gain need the same length, at least 2");
    if (freq_in.front() != 0 || freq_in.back() != 1) return make_error(Errc::invalid_argument, "firwin2: freq must start at 0 and end at 1 (Nyquist)");
    for (std::size_t i = 1; i < freq_in.size(); ++i)
        if (freq_in[i] < freq_in[i - 1]) return make_error(Errc::invalid_argument, "firwin2: freq must not decrease");
    if (numtaps % 2 == 0 && gain.back() != 0) return make_error(Errc::invalid_argument, "firwin2: an even number of taps needs zero gain at Nyquist");
    std::vector<double> freq(freq_in.begin(), freq_in.end());
    const double eps = std::numeric_limits<double>::epsilon();
    for (std::size_t k = 0; k + 1 < freq.size(); ++k)
        if (freq[k] == freq[k + 1]) {
            freq[k] -= eps;
            freq[k + 1] += eps;
        }
    const auto nfreqs = 1 + (std::size_t{1} << static_cast<int>(std::ceil(std::log2(static_cast<double>(numtaps)))));
    std::vector<std::complex<double>> fx(nfreqs);
    for (std::size_t i = 0; i < nfreqs; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(nfreqs - 1);
        auto it = std::upper_bound(freq.begin(), freq.end(), x);
        double g;
        if (it == freq.begin()) g = gain.front();
        else if (it == freq.end()) g = gain.back();
        else {
            const auto j = static_cast<std::size_t>(it - freq.begin());
            const double t = (x - freq[j - 1]) / (freq[j] - freq[j - 1]);
            g = gain[j - 1] + t * (gain[j] - gain[j - 1]);
        }
        fx[i] = g * std::polar(1.0, -0.5 * (numtaps - 1) * kPi * x);
    }
    const std::size_t full = 2 * (nfreqs - 1);
    std::vector<double> out(full);
    const pocketfft::shape_t shape{full};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)}, real_stride{sizeof(double)};
    pocketfft::c2r(shape, complex_stride, real_stride, 0, pocketfft::BACKWARD, fx.data(), out.data(), 1.0 / static_cast<double>(full));
    out.resize(static_cast<std::size_t>(numtaps));
    const auto w = symmetric_window(window, out.size(), beta);
    for (std::size_t k = 0; k < out.size(); ++k) out[k] *= w[k];
    return out;
}

}  // namespace signals::core
