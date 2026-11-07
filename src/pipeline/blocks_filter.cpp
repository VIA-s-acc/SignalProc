#include <array>
#include <cmath>

#include "core/conv.hpp"
#include "core/design/fir.hpp"
#include "core/design/iir.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;
using Sos = core::Sos;
using State = std::vector<std::array<double, 2>>;

const std::vector<std::string> kWindows{"hamming", "hann", "blackman", "kaiser", "boxcar"};

core::Result<std::vector<double>> normalized_cutoffs(std::string_view block, const Params& p, double rate) {
    auto hz = parse_numbers(block, "cutoff", param_text(p, "cutoff"));
    if (!hz) return hz;
    for (auto& f : *hz) {
        if (!(f > 0 && f < rate / 2)) return fail(tr("pipeline.error.cutoff_range"), fmt::arg("block", block), fmt::arg("f", f), fmt::arg("nyquist", rate / 2));
        f /= rate / 2;
    }
    return hz;
}

Samples sosfilt(std::span<const Sos> sos, std::span<const double> x, State& z) {
    Samples y(x.begin(), x.end());
    for (std::size_t s = 0; s < sos.size(); ++s) {
        const auto& [b0, b1, b2, a0, a1, a2] = sos[s];
        auto& [z0, z1] = z[s];
        for (auto& v : y) {
            const double in = v;
            v = b0 * in + z0;
            z0 = b1 * in - a1 * v + z1;
            z1 = b2 * in - a2 * v;
        }
    }
    return y;
}

State steady_state(std::span<const Sos> sos, double input) {
    State z(sos.size());
    double scale = input;
    for (std::size_t s = 0; s < sos.size(); ++s) {
        const auto& [b0, b1, b2, a0, a1, a2] = sos[s];
        const double r0 = b1 - a1 * b0, r1 = b2 - a2 * b0;
        const double det = 1 + a1 + a2;
        z[s] = {scale * (r0 + r1) / det, scale * (r1 * (1 + a1) - a2 * r0) / det};
        scale *= (b0 + b1 + b2) / (1 + a1 + a2);
    }
    return z;
}

std::size_t filtfilt_edge(std::span<const Sos> sos) {
    std::size_t b_short = 0, a_short = 0;
    for (const auto& s : sos) {
        b_short += s[2] == 0;
        a_short += s[5] == 0;
    }
    return 3 * (2 * sos.size() + 1 - std::min(b_short, a_short));
}

core::Result<Samples> sosfiltfilt(std::span<const Sos> sos, const Samples& x) {
    const auto edge = filtfilt_edge(sos);
    if (x.size() <= edge) return fail(tr("pipeline.error.filtfilt_short"), fmt::arg("min", edge + 1), fmt::arg("n", x.size()));
    Samples ext;
    ext.reserve(x.size() + 2 * edge);
    for (std::size_t i = edge; i >= 1; --i) ext.push_back(2 * x.front() - x[i]);
    ext.insert(ext.end(), x.begin(), x.end());
    for (std::size_t i = 1; i <= edge; ++i) ext.push_back(2 * x.back() - x[x.size() - 1 - i]);
    auto z = steady_state(sos, ext.front());
    auto y = sosfilt(sos, ext, z);
    std::ranges::reverse(y);
    z = steady_state(sos, y.front());
    y = sosfilt(sos, y, z);
    std::ranges::reverse(y);
    return Samples(y.begin() + static_cast<std::ptrdiff_t>(edge), y.end() - static_cast<std::ptrdiff_t>(edge));
}

Samples slice_full(const Samples& full, std::size_t start, std::size_t n) {
    Samples y(n, 0.0);
    for (std::size_t i = 0; i < n && start + i < full.size(); ++i) y[i] = full[start + i];
    return y;
}

Out fir_apply(std::span<const Value> in, const Samples& h, bool centred) {
    const std::size_t start = centred ? (h.size() - 1) / 2 : 0;
    return each_channel(audio(in), [&](const Samples& c) { return c.empty() ? c : slice_full(core::convolve(core::Laurent{c, 0}, core::Laurent{h, 0}).coeffs, start, c.size()); });
}

Samples median_filter(const Samples& x, std::size_t kernel) {
    const auto half = kernel / 2;
    Samples padded(half, 0.0);
    padded.insert(padded.end(), x.begin(), x.end());
    padded.resize(padded.size() + half, 0.0);
    Samples window(padded.begin(), padded.begin() + static_cast<std::ptrdiff_t>(std::min(kernel, padded.size())));
    std::ranges::sort(window);
    Samples y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (i > 0) {
            window.erase(std::ranges::lower_bound(window, padded[i - 1]));
            const double next = padded[i + kernel - 1];
            window.insert(std::ranges::upper_bound(window, next), next);
        }
        y[i] = window[half];
    }
    return y;
}

}  // namespace

void register_filter_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"iir",
                           kIn,
                           kOut,
                           {{"family", Kind::choice, "butterworth", {"butterworth", "cheby1", "cheby2", "ellip", "bessel"}},
                            {"type", Kind::choice, "lowpass", {"lowpass", "highpass", "bandpass", "bandstop"}},
                            {"order", Kind::integer, "4", {}, 1, 32},
                            {"cutoff", Kind::text, "1000"},
                            {"rp", Kind::real, "1", {}, 0.01, 30},
                            {"rs", Kind::real, "40", {}, 1, 200},
                            {"zero_phase", Kind::choice, "no", {"no", "yes"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               core::IirSpec spec;
                               spec.family = *core::parse_iir_family(param_text(p, "family"));
                               spec.type = *core::parse_band_type(param_text(p, "type"));
                               spec.order = static_cast<int>(param_int(p, "order"));
                               spec.rp = param_real(p, "rp");
                               spec.rs = param_real(p, "rs");
                               auto wn = normalized_cutoffs("iir", p, a.sample_rate);
                               if (!wn) return std::unexpected{wn.error()};
                               const bool band = spec.type == core::BandType::bandpass || spec.type == core::BandType::bandstop;
                               if (wn->size() != (band ? 2U : 1U)) return fail(tr("pipeline.error.iir_cutoffs"), fmt::arg("count", wn->size()));
                               spec.wn = std::move(*wn);
                               const auto zpk = core::iir_design(spec);
                               if (!zpk) return std::unexpected{zpk.error()};
                               if (!core::is_stable(*zpk)) return fail(tr("pipeline.error.unstable"));
                               const auto sos = core::zpk_to_sos(*zpk);
                               const bool zero_phase = param_text(p, "zero_phase") == "yes";
                               return each_channel(a, [&](const Samples& c) -> core::Result<Samples> {
                                   if (zero_phase) return sosfiltfilt(sos, c);
                                   State z(sos.size());
                                   return sosfilt(sos, c, z);
                               });
                           }}));
    out.push_back(checked({"fir",
                           kIn,
                           kOut,
                           {{"numtaps", Kind::integer, "101", {}, 1, 8191},
                            {"cutoff", Kind::text, "1000"},
                            {"window", Kind::choice, "hamming", kWindows},
                            {"beta", Kind::real, "8.6", {}, 0, 50},
                            {"pass_zero", Kind::choice, "yes", {"yes", "no"}},
                            {"compensate", Kind::choice, "no", {"no", "yes"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               auto wn = normalized_cutoffs("fir", p, audio(in).sample_rate);
                               if (!wn) return std::unexpected{wn.error()};
                               auto h = core::firwin(static_cast<int>(param_int(p, "numtaps")), *wn, *core::parse_fir_window(param_text(p, "window")),
                                                     param_text(p, "pass_zero") == "yes", param_real(p, "beta"));
                               if (!h) return std::unexpected{h.error()};
                               return fir_apply(in, *h, param_text(p, "compensate") == "yes");
                           }}));
    out.push_back(checked({"median", kIn, kOut, {{"kernel", Kind::integer, "5", {}, 1, 4095}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto kernel = static_cast<std::size_t>(param_int(p, "kernel"));
                               if (kernel % 2 == 0) return fail(tr("pipeline.error.median_odd"), fmt::arg("value", kernel));
                               return each_channel(audio(in), [&](const Samples& c) { return median_filter(c, kernel); });
                           }}));
    out.push_back(checked({"moving_average",
                           kIn,
                           kOut,
                           {{"length", Kind::integer, "5", {}, 1, 65536}, {"centred", Kind::choice, "no", {"no", "yes"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto length = static_cast<std::size_t>(param_int(p, "length"));
                               return fir_apply(in, Samples(length, 1.0 / static_cast<double>(length)), param_text(p, "centred") == "yes");
                           }}));
    out.push_back(checked({"preemphasis", kIn, kOut, {{"coeff", Kind::real, "0.97", {}, 0, 1}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double k = param_real(p, "coeff");
                               return each_channel(audio(in), [k](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = c[i] - (i ? k * c[i - 1] : 0.0);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"deemphasis", kIn, kOut, {{"coeff", Kind::real, "0.97", {}, 0, 0.9999}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double k = param_real(p, "coeff");
                               return each_channel(audio(in), [k](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = c[i] + (i ? k * y[i - 1] : 0.0);
                                   return y;
                               });
                           }}));
}

}  // namespace signals::pipeline
