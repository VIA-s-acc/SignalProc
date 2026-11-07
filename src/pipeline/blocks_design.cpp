#include <algorithm>
#include <cmath>
#include <complex>

#include <Eigen/Dense>

#include "core/analysis.hpp"
#include "core/conv.hpp"
#include "core/design/fir.hpp"
#include "core/design/roots.hpp"
#include "core/freq.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

constexpr std::size_t kMaxTaps = 65536;
constexpr std::size_t kMaxPoles = 128;
constexpr double kPoleSlack = 1e-6;

const std::vector<std::string> kNoYes{"no", "yes"};

core::Result<std::vector<double>> coefficients(std::string_view block, const std::string& name, const Params& p, std::size_t limit) {
    auto v = parse_numbers(block, name, param_text(p, name));
    if (v && v->size() > limit)
        return fail(tr("pipeline.error.too_many_coeffs"), fmt::arg("block", block), fmt::arg("param", name), fmt::arg("n", v->size()), fmt::arg("max", limit));
    return v;
}

Samples slice_full(const Samples& full, std::size_t start, std::size_t n) {
    Samples y(n, 0.0);
    for (std::size_t i = 0; i < n && start + i < full.size(); ++i) y[i] = full[start + i];
    return y;
}

Samples convolve_prefix(const Samples& x, const Samples& h, std::size_t start) {
    return x.empty() ? x : slice_full(core::convolve(core::Laurent{x, 0}, core::Laurent{h, 0}).coeffs, start, x.size());
}

Out fir_apply(const core::AudioBuffer& a, const Samples& h, bool centred) {
    const std::size_t start = centred ? (h.size() - 1) / 2 : 0;
    return each_channel(a, [&](const Samples& c) { return convolve_prefix(c, h, start); });
}

core::Result<double> largest_pole(const std::vector<double>& a) {
    const auto roots = core::polynomial_roots<double>(std::vector<double>(a.rbegin(), a.rend()));
    if (!roots) return std::unexpected{roots.error()};
    double r = 0;
    for (const auto& z : *roots) r = std::max(r, core::modulus(z));
    return r;
}

core::Result<std::vector<double>> band_edges(std::string_view block, const Params& p, double rate) {
    if (!(rate > 0)) return fail(tr("pipeline.error.needs_rate"), fmt::arg("block", block));
    std::string text = param_text(p, "bands");
    const auto nyquist = fmt::format("{:.17g}", rate / 2);
    for (auto at = text.find("nyquist"); at != std::string::npos; at = text.find("nyquist", at + nyquist.size())) text.replace(at, 7, nyquist);
    auto hz = parse_numbers(block, "bands", text);
    if (!hz) return fail(tr("pipeline.error.numbers"), fmt::arg("block", block), fmt::arg("param", "bands"), fmt::arg("value", param_text(p, "bands")));
    for (const double f : *hz)
        if (!(f >= 0 && f <= rate / 2)) return fail(tr("pipeline.error.band_edge"), fmt::arg("block", block), fmt::arg("f", f), fmt::arg("nyquist", rate / 2));
    return hz;
}

core::Result<std::vector<double>> weights(std::string_view block, const Params& p) {
    const auto& text = param_text(p, "weight");
    if (text.find_first_not_of(" ,") == std::string::npos) return std::vector<double>{};
    return parse_numbers(block, "weight", text);
}

enum class Edge { interp, mirror, nearest, constant, wrap };

Edge parse_edge(const std::string& name) {
    if (name == "mirror") return Edge::mirror;
    if (name == "nearest") return Edge::nearest;
    if (name == "constant") return Edge::constant;
    if (name == "wrap") return Edge::wrap;
    return Edge::interp;
}

double extended(const Samples& x, std::ptrdiff_t i, Edge edge) {
    const auto n = static_cast<std::ptrdiff_t>(x.size());
    if (i >= 0 && i < n) return x[static_cast<std::size_t>(i)];
    switch (edge) {
    case Edge::nearest: return i < 0 ? x.front() : x.back();
    case Edge::wrap: return x[static_cast<std::size_t>((i % n + n) % n)];
    case Edge::mirror: {
        if (n == 1) return x.front();
        const auto period = 2 * (n - 1);
        const auto j = (i % period + period) % period;
        return x[static_cast<std::size_t>(j < n ? j : period - j)];
    }
    case Edge::interp:
    case Edge::constant: return 0.0;
    }
    return 0.0;
}

// The fit uses u = (k - centre) / scale in [-1, 1] instead of raw sample offsets, which keeps the Vandermonde matrix
// well conditioned up to polyorder 20; the derivative is rescaled by (rate / scale)^deriv afterwards.
class Savgol {
public:
    Savgol(std::size_t window, std::size_t order, std::size_t deriv, double rate)
        : window_{window}, order_{order}, deriv_{deriv}, centre_{static_cast<double>(window - 1) / 2}, scale_{std::max(1.0, centre_)},
          factor_{std::pow(rate / scale_, static_cast<double>(deriv))}, qr_{vandermonde()} {
        const auto m = static_cast<Eigen::Index>(order + 1);
        const Eigen::MatrixXd r = qr_.matrixQR().topLeftCorner(m, m).triangularView<Eigen::Upper>();
        Eigen::VectorXd z = r.transpose().triangularView<Eigen::Lower>().solve(derivative_row(0.0));
        Eigen::VectorXd padded = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(window));
        padded.head(m) = z;
        const Eigen::VectorXd h = qr_.householderQ() * padded;
        centred_.resize(window);
        for (std::size_t k = 0; k < window; ++k) centred_[window - 1 - k] = h(static_cast<Eigen::Index>(k)) * factor_;
    }

    Samples apply(const Samples& x, Edge edge) const {
        if (x.empty()) return x;
        const auto half = static_cast<std::ptrdiff_t>(window_ / 2);
        Samples padded(x.size() + window_ - 1);
        for (std::size_t j = 0; j < padded.size(); ++j) padded[j] = extended(x, static_cast<std::ptrdiff_t>(j) - half, edge);
        Samples y = convolve_prefix(padded, centred_, window_ - 1);
        y.resize(x.size());
        if (edge == Edge::interp) {
            fit_edge(x, 0, 0, window_ / 2, y);
            fit_edge(x, x.size() - window_, x.size() - window_ / 2, x.size(), y);
        }
        return y;
    }

private:
    Eigen::MatrixXd vandermonde() const {
        Eigen::MatrixXd v(static_cast<Eigen::Index>(window_), static_cast<Eigen::Index>(order_ + 1));
        for (std::size_t k = 0; k < window_; ++k) {
            const double u = (static_cast<double>(k) - centre_) / scale_;
            double power = 1;
            for (std::size_t j = 0; j <= order_; ++j, power *= u) v(static_cast<Eigen::Index>(k), static_cast<Eigen::Index>(j)) = power;
        }
        return v;
    }

    Eigen::VectorXd derivative_row(double u) const {
        Eigen::VectorXd w = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(order_ + 1));
        for (std::size_t j = deriv_; j <= order_; ++j) {
            double falling = 1;
            for (std::size_t q = 0; q < deriv_; ++q) falling *= static_cast<double>(j - q);
            w(static_cast<Eigen::Index>(j)) = falling * std::pow(u, static_cast<double>(j - deriv_));
        }
        return w;
    }

    void fit_edge(const Samples& x, std::size_t start, std::size_t from, std::size_t to, Samples& y) const {
        Eigen::VectorXd segment(static_cast<Eigen::Index>(window_));
        for (std::size_t k = 0; k < window_; ++k) segment(static_cast<Eigen::Index>(k)) = x[start + k];
        const Eigen::VectorXd beta = qr_.solve(segment);
        for (std::size_t i = from; i < to; ++i) y[i] = derivative_row((static_cast<double>(i - start) - centre_) / scale_).dot(beta) * factor_;
    }

    std::size_t window_, order_, deriv_;
    double centre_, scale_, factor_;
    Eigen::HouseholderQR<Eigen::MatrixXd> qr_;
    Samples centred_;
};

Samples analytic_part(const Samples& x, const std::string& output) {
    const auto z = core::analytic_signal(x);
    Samples y(z.size());
    for (std::size_t i = 0; i < z.size(); ++i) y[i] = output == "imag" ? z[i].imag() : std::arg(z[i]);
    return output == "unwrapped" ? core::unwrap_phase(y) : y;
}

}  // namespace

void register_design_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"lfilter", kIn, kOut, {{"b", Kind::text, "1"}, {"a", Kind::text, "1"}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               auto b = coefficients("lfilter", "b", p, kMaxTaps);
                               if (!b) return std::unexpected{b.error()};
                               auto a = coefficients("lfilter", "a", p, kMaxPoles);
                               if (!a) return std::unexpected{a.error()};
                               const double a0 = a->front();
                               if (a0 == 0) return fail(tr("pipeline.error.a0_zero"));
                               for (auto& v : *b) v /= a0;
                               for (auto& v : *a) v /= a0;
                               const auto pole = largest_pole(*a);
                               if (!pole) return std::unexpected{pole.error()};
                               if (*pole > 1 + kPoleSlack) return fail(tr("pipeline.error.lfilter_unstable"), fmt::arg("r", fmt::format("{:.6g}", *pole)));
                               return each_channel(audio(in), [&](const Samples& c) {
                                   Samples y = convolve_prefix(c, *b, 0);
                                   for (std::size_t i = 0; i < y.size(); ++i) {
                                       double acc = y[i];
                                       for (std::size_t k = 1; k < a->size() && k <= i; ++k) acc -= (*a)[k] * y[i - k];
                                       y[i] = acc;
                                   }
                                   return y;
                               });
                           }}));
    out.push_back(checked({"fir_taps",
                           kIn,
                           kOut,
                           {{"taps", Kind::text, "1"}, {"compensate", Kind::choice, "no", kNoYes}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               auto h = coefficients("fir_taps", "taps", p, kMaxTaps);
                               if (!h) return std::unexpected{h.error()};
                               return fir_apply(audio(in), *h, param_text(p, "compensate") == "yes");
                           }}));
    out.push_back(checked({"savgol",
                           kIn,
                           kOut,
                           {{"window", Kind::integer, "11", {}, 1, 4095},
                            {"polyorder", Kind::integer, "3", {}, 0, 20},
                            {"deriv", Kind::integer, "0", {}, 0, 20},
                            {"mode", Kind::choice, "interp", {"interp", "mirror", "nearest", "constant", "wrap"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto window = static_cast<std::size_t>(param_int(p, "window"));
                               const auto order = static_cast<std::size_t>(param_int(p, "polyorder"));
                               const auto deriv = static_cast<std::size_t>(param_int(p, "deriv"));
                               if (window % 2 == 0) return fail(tr("pipeline.error.savgol_window"), fmt::arg("value", window));
                               if (order >= window || deriv > order)
                                   return fail(tr("pipeline.error.savgol_order"), fmt::arg("order", order), fmt::arg("window", window), fmt::arg("deriv", deriv));
                               if (deriv > 0 && !(a.sample_rate > 0)) return fail(tr("pipeline.error.needs_rate"), fmt::arg("block", "savgol"));
                               const auto edge = parse_edge(param_text(p, "mode"));
                               const Savgol filter{window, order, deriv, deriv > 0 ? a.sample_rate : 1.0};
                               return each_channel(a, [&](const Samples& c) -> core::Result<Samples> {
                                   if (edge == Edge::interp && !c.empty() && c.size() < window)
                                       return fail(tr("pipeline.error.savgol_short"), fmt::arg("window", window), fmt::arg("n", c.size()));
                                   return filter.apply(c, edge);
                               });
                           }}));
    out.push_back(checked({"hilbert",
                           kIn,
                           kOut,
                           {{"output", Kind::choice, "imag", {"imag", "phase", "unwrapped"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& output = param_text(p, "output");
                               return each_channel(audio(in), [&](const Samples& c) { return analytic_part(c, output); });
                           }}));
    out.push_back(checked({"remez",
                           kIn,
                           kOut,
                           {{"numtaps", Kind::integer, "63", {}, 3, 4095},
                            {"bands", Kind::text, "0 1000 1500 nyquist"},
                            {"desired", Kind::text, "1 0"},
                            {"weight", Kind::text, ""},
                            {"type", Kind::choice, "bandpass", {"bandpass", "differentiator", "hilbert"}},
                            {"compensate", Kind::choice, "no", kNoYes}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               auto bands = band_edges("remez", p, a.sample_rate);
                               if (!bands) return std::unexpected{bands.error()};
                               auto desired = parse_numbers("remez", "desired", param_text(p, "desired"));
                               if (!desired) return std::unexpected{desired.error()};
                               auto weight = weights("remez", p);
                               if (!weight) return std::unexpected{weight.error()};
                               core::RemezSpec spec;
                               spec.numtaps = static_cast<int>(param_int(p, "numtaps"));
                               for (const double f : *bands) spec.bands.push_back(f / a.sample_rate);
                               spec.desired = std::move(*desired);
                               spec.weight = std::move(*weight);
                               spec.type = *core::parse_remez_type(param_text(p, "type"));
                               const auto h = core::remez(spec);
                               if (!h) return std::unexpected{h.error()};
                               return fir_apply(a, *h, param_text(p, "compensate") == "yes");
                           }}));
    out.push_back(checked({"firls",
                           kIn,
                           kOut,
                           {{"numtaps", Kind::integer, "63", {}, 1, 4095},
                            {"bands", Kind::text, "0 1000 1500 nyquist"},
                            {"desired", Kind::text, "1 1 0 0"},
                            {"weight", Kind::text, ""},
                            {"compensate", Kind::choice, "no", kNoYes}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               auto bands = band_edges("firls", p, a.sample_rate);
                               if (!bands) return std::unexpected{bands.error()};
                               auto desired = parse_numbers("firls", "desired", param_text(p, "desired"));
                               if (!desired) return std::unexpected{desired.error()};
                               auto weight = weights("firls", p);
                               if (!weight) return std::unexpected{weight.error()};
                               for (double& f : *bands) f /= a.sample_rate / 2;
                               const auto h = core::firls(static_cast<int>(param_int(p, "numtaps")), *bands, *desired, *weight);
                               if (!h) return std::unexpected{h.error()};
                               return fir_apply(a, *h, param_text(p, "compensate") == "yes");
                           }}));
}

}  // namespace signals::pipeline
