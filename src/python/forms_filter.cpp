#include <string>
#include <vector>

#include <fmt/format.h>

#include "pipeline/blocks_common.hpp"
#include "python/form.hpp"

namespace signals::python {
namespace {

using pipeline::param_int;
using pipeline::param_real;
using pipeline::param_text;

double rate_in(const NodeIo& io) {
    if (!io.in_values.empty() && io.in_values[0])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[0])) return a->sample_rate;
    return 44100;
}

std::string list(Emitter& em, std::string key, const std::vector<double>& v) { return em.numbers(std::move(key), v); }

core::Result<std::vector<double>> numbers_of(const NodeIo& io, std::string_view param, std::string text) {
    auto v = pipeline::blocks::parse_numbers(io.node.type->id, std::string{param}, text);
    if (!v) return std::unexpected{unsupported(io, param)};
    return v;
}

std::string cutoffs(const NodeIo& io, Emitter& em, const std::vector<double>& hz) {
    std::string out = "[";
    for (std::size_t k = 0; k < hz.size(); ++k) out += (k ? ", " : "") + em.number(hz[k]) + " / (" + io.in_fs[0] + " / 2)";
    return out + "]";
}

core::Result<std::string> iir(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto hz = numbers_of(io, "cutoff", param_text(p, "cutoff"));
    if (!hz) return std::unexpected{hz.error()};
    const auto& family = param_text(p, "family");
    const std::string ftype = family == "butterworth" ? "butter" : family;
    em.import_line("from scipy import signal");
    const auto sos = fmt::format("signal.iirfilter({}, {}, rp={}, rs={}, btype={}, ftype={}, output=\"sos\")", param_int(p, "order"), cutoffs(io, em, *hz),
                                 em.number(param_real(p, "rp")), em.number(param_real(p, "rs")), py_str(param_text(p, "type")), py_str(ftype));
    const char* apply = param_text(p, "zero_phase") == "yes" ? "sosfiltfilt" : "sosfilt";
    return fmt::format("{0}_sos = {1}\n{0} = signal.{2}({0}_sos, {3}, axis=1)", io.out[0], sos, apply, io.in[0]);
}

core::Result<std::string> fir(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto hz = numbers_of(io, "cutoff", param_text(p, "cutoff"));
    if (!hz) return std::unexpected{hz.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    const auto& window = param_text(p, "window");
    const auto w = window == "kaiser" ? fmt::format("(\"kaiser\", {})", em.number(param_real(p, "beta"))) : py_str(window);
    return fmt::format("{0}_h = signal.firwin({1}, {2}, window={3}, pass_zero={4})\n{0} = fir_apply({5}, {0}_h, {6})", io.out[0], param_int(p, "numtaps"), cutoffs(io, em, *hz), w,
                       param_text(p, "pass_zero") == "yes" ? "True" : "False", io.in[0], param_text(p, "compensate") == "yes" ? "True" : "False");
}

core::Result<std::string> median(const NodeIo& io, Emitter& em) {
    em.function("median_filter");
    return fmt::format("{} = median_filter({}, {})", io.out[0], io.in[0], param_int(io.node.params, "kernel"));
}

core::Result<std::string> moving_average(const NodeIo& io, Emitter& em) {
    em.function("fir_apply");
    const auto n = param_int(io.node.params, "length");
    return fmt::format("{} = fir_apply({}, np.full({}, 1.0 / {}), {})", io.out[0], io.in[0], n, n, param_text(io.node.params, "centred") == "yes" ? "True" : "False");
}

core::Result<std::string> preemphasis(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = np.concatenate([{1}[:, :1], {1}[:, 1:] - {2} * {1}[:, :-1]], axis=1)", io.out[0], io.in[0], em.number(param_real(io.node.params, "coeff")));
}

core::Result<std::string> deemphasis(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    return fmt::format("{} = signal.lfilter([1.0], [1.0, -{}], {}, axis=1)", io.out[0], em.number(param_real(io.node.params, "coeff")), io.in[0]);
}

core::Result<std::string> lfilter(const NodeIo& io, Emitter& em) {
    const auto b = numbers_of(io, "b", param_text(io.node.params, "b"));
    const auto a = numbers_of(io, "a", param_text(io.node.params, "a"));
    if (!b) return std::unexpected{b.error()};
    if (!a) return std::unexpected{a.error()};
    em.import_line("from scipy import signal");
    return fmt::format("{} = signal.lfilter({}, {}, {}, axis=1)", io.out[0], list(em, "b", *b), list(em, "a", *a), io.in[0]);
}

core::Result<std::string> fir_taps(const NodeIo& io, Emitter& em) {
    const auto h = numbers_of(io, "taps", param_text(io.node.params, "taps"));
    if (!h) return std::unexpected{h.error()};
    em.function("fir_apply");
    return fmt::format("{} = fir_apply({}, {}, {})", io.out[0], io.in[0], list(em, "taps", *h), param_text(io.node.params, "compensate") == "yes" ? "True" : "False");
}

core::Result<std::string> savgol(const NodeIo& io, Emitter&) {
    const auto& p = io.node.params;
    const auto deriv = param_int(p, "deriv");
    return fmt::format("{} = signal.savgol_filter({}, {}, {}, deriv={}, delta={}, mode={}, axis=1)", io.out[0], io.in[0], param_int(p, "window"), param_int(p, "polyorder"), deriv,
                       deriv > 0 ? "1 / " + io.in_fs[0] : std::string{"1.0"}, py_str(param_text(p, "mode")));
}

core::Result<std::string> hilbert(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    const auto& output = param_text(io.node.params, "output");
    const auto z = fmt::format("signal.hilbert({}, axis=1)", io.in[0]);
    if (output == "imag") return fmt::format("{} = np.imag({})", io.out[0], z);
    if (output == "phase") return fmt::format("{} = np.angle({})", io.out[0], z);
    return fmt::format("{} = np.unwrap(np.angle({}), axis=1)", io.out[0], z);
}

core::Result<std::vector<double>> band_edges(const NodeIo& io) {
    std::string text = param_text(io.node.params, "bands");
    const auto nyquist = fmt::format("{:.17g}", rate_in(io) / 2);
    for (auto at = text.find("nyquist"); at != std::string::npos; at = text.find("nyquist", at + nyquist.size())) text.replace(at, 7, nyquist);
    return numbers_of(io, "bands", text);
}

std::string weight_arg(const NodeIo& io, Emitter& em) {
    const auto& text = param_text(io.node.params, "weight");
    if (text.find_first_not_of(" ,") == std::string::npos) return "None";
    const auto w = pipeline::blocks::parse_numbers(io.node.type->id, "weight", text);
    return w ? list(em, "weight", *w) : std::string{"None"};
}

core::Result<std::string> remez(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto bands = band_edges(io);
    const auto desired = numbers_of(io, "desired", param_text(p, "desired"));
    if (!bands) return std::unexpected{bands.error()};
    if (!desired) return std::unexpected{desired.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    std::string edges = "[";
    for (std::size_t k = 0; k < bands->size(); ++k) edges += (k ? ", " : "") + em.number((*bands)[k]) + " / " + io.in_fs[0];
    edges += "]";
    return fmt::format("{0}_h = signal.remez({1}, {2}, {3}, weight={4}, type={5})\n{0} = fir_apply({6}, {0}_h, {7})", io.out[0], param_int(p, "numtaps"), edges,
                       list(em, "desired", *desired), weight_arg(io, em), py_str(param_text(p, "type")), io.in[0], param_text(p, "compensate") == "yes" ? "True" : "False");
}

core::Result<std::string> firls(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto bands = band_edges(io);
    const auto desired = numbers_of(io, "desired", param_text(p, "desired"));
    if (!bands) return std::unexpected{bands.error()};
    if (!desired) return std::unexpected{desired.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    std::string edges = "[";
    for (std::size_t k = 0; k < bands->size(); ++k) edges += (k ? ", " : "") + em.number((*bands)[k]) + " / (" + io.in_fs[0] + " / 2)";
    edges += "]";
    return fmt::format("{0}_h = signal.firls({1}, {2}, {3}, weight={4})\n{0} = fir_apply({5}, {0}_h, {6})", io.out[0], param_int(p, "numtaps"), edges, list(em, "desired", *desired),
                       weight_arg(io, em), io.in[0], param_text(p, "compensate") == "yes" ? "True" : "False");
}

}  // namespace

void register_filter_forms(std::vector<PyForm>& out) {
    out.push_back({"iir", 1e-8, iir});
    out.push_back({"fir", 1e-12, fir});
    out.push_back({"median", 1e-12, median});
    out.push_back({"moving_average", 1e-12, moving_average});
    out.push_back({"preemphasis", 1e-12, preemphasis});
    out.push_back({"deemphasis", 1e-12, deemphasis});
    out.push_back({"lfilter", 1e-12, lfilter});
    out.push_back({"fir_taps", 1e-12, fir_taps});
    out.push_back({"savgol", 1e-12, [](const NodeIo& io, Emitter& em) {
                       em.import_line("from scipy import signal");
                       return savgol(io, em);
                   }});
    out.push_back({"hilbert", 1e-12, hilbert});
    out.push_back({"remez", 1e-8, remez});
    out.push_back({"firls", 1e-10, firls});
}

}  // namespace signals::python
