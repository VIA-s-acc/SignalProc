#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/design/families.hpp"
#include "core/extension.hpp"
#include "core/process/shrink.hpp"
#include "core/swt.hpp"
#include "i18n/i18n.hpp"
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

core::Result<std::string> number_list(const NodeIo& io, Emitter& em, std::string_view param) {
    if (uses_variable(io, param)) {
        em.function("number_list");
        return "number_list(" + em.expr(io, param) + ")";
    }
    const auto v = numbers_of(io, param, param_text(io.node.params, param));
    if (!v) return std::unexpected{v.error()};
    return list(em, std::string{param}, *v);
}

std::string flag(const NodeIo& io, Emitter& em, std::string_view param, std::string_view yes) {
    if (uses_variable(io, param)) return "(" + em.expr(io, param) + " == " + py_str(yes) + ")";
    return param_text(io.node.params, param) == yes ? "True" : "False";
}

std::string cutoffs(const NodeIo& io, Emitter& em, const std::vector<double>& hz) {
    std::string out = "[";
    for (std::size_t k = 0; k < hz.size(); ++k) out += (k ? ", " : "") + em.number(hz[k]) + " / (" + io.in_fs[0] + " / 2)";
    return out + "]";
}

core::Result<std::string> cutoff_list(const NodeIo& io, Emitter& em) {
    if (uses_variable(io, "cutoff")) {
        em.function("number_list");
        return fmt::format("[f / ({} / 2) for f in number_list({})]", io.in_fs[0], em.expr(io, "cutoff"));
    }
    const auto hz = numbers_of(io, "cutoff", param_text(io.node.params, "cutoff"));
    if (!hz) return std::unexpected{hz.error()};
    return cutoffs(io, em, *hz);
}

core::Result<std::string> iir(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto cut = cutoff_list(io, em);
    if (!cut) return std::unexpected{cut.error()};
    const auto& family = param_text(p, "family");
    const auto ftype = uses_variable(io, "family") ? fmt::format("{{\"butterworth\": \"butter\"}}.get({0}, {0})", em.expr(io, "family"))
                                                   : py_str(family == "butterworth" ? std::string{"butter"} : family);
    em.import_line("from scipy import signal");
    const auto sos = fmt::format("signal.iirfilter({}, {}, rp={}, rs={}, btype={}, ftype={}, output=\"sos\")", em.expr(io, "order"), *cut, em.expr(io, "rp"), em.expr(io, "rs"),
                                 em.expr(io, "type"), ftype);
    const auto apply = uses_variable(io, "zero_phase") ? "(signal.sosfiltfilt if " + em.expr(io, "zero_phase") + " == \"yes\" else signal.sosfilt)"
                                                       : std::string{param_text(p, "zero_phase") == "yes" ? "signal.sosfiltfilt" : "signal.sosfilt"};
    return fmt::format("{0}_sos = {1}\n{0} = {2}({0}_sos, {3}, axis=1)", io.out[0], sos, apply, io.in[0]);
}

core::Result<std::string> fir(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto cut = cutoff_list(io, em);
    if (!cut) return std::unexpected{cut.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    const auto& window = param_text(p, "window");
    std::string w;
    if (uses_variable(io, "window") || uses_variable(io, "beta")) w = fmt::format("((\"kaiser\", {}) if {} == \"kaiser\" else {})", em.expr(io, "beta"), em.expr(io, "window"), em.expr(io, "window"));
    else w = window == "kaiser" ? fmt::format("(\"kaiser\", {})", em.expr(io, "beta")) : py_str(window);
    return fmt::format("{0}_h = signal.firwin({1}, {2}, window={3}, pass_zero={4})\n{0} = fir_apply({5}, {0}_h, {6})", io.out[0], em.expr(io, "numtaps"), *cut, w,
                       flag(io, em, "pass_zero", "yes"), io.in[0], flag(io, em, "compensate", "yes"));
}

core::Result<std::string> median(const NodeIo& io, Emitter& em) {
    em.function("median_filter");
    return fmt::format("{} = median_filter({}, {})", io.out[0], io.in[0], em.expr(io, "length"));
}

core::Result<std::string> moving_average(const NodeIo& io, Emitter& em) {
    em.function("fir_apply");
    const auto n = em.expr(io, "length");
    return fmt::format("{} = fir_apply({}, np.full({}, 1.0 / {}), {})", io.out[0], io.in[0], n, n, flag(io, em, "compensate", "yes"));
}

core::Result<std::string> preemphasis(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = np.concatenate([{1}[:, :1], {1}[:, 1:] - {2} * {1}[:, :-1]], axis=1)", io.out[0], io.in[0], em.expr(io, "coeff"));
}

core::Result<std::string> deemphasis(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    const auto c = em.expr(io, "coeff");
    return fmt::format("{} = signal.lfilter([1.0], [1.0, -{}], {}, axis=1)", io.out[0], uses_variable(io, "coeff") ? "(" + c + ")" : c, io.in[0]);
}

core::Result<std::string> lfilter(const NodeIo& io, Emitter& em) {
    const auto b = number_list(io, em, "b");
    const auto a = number_list(io, em, "a");
    if (!b) return std::unexpected{b.error()};
    if (!a) return std::unexpected{a.error()};
    em.import_line("from scipy import signal");
    return fmt::format("{} = signal.lfilter({}, {}, {}, axis=1)", io.out[0], *b, *a, io.in[0]);
}

core::Result<std::string> fir_taps(const NodeIo& io, Emitter& em) {
    const auto h = number_list(io, em, "taps");
    if (!h) return std::unexpected{h.error()};
    em.function("fir_apply");
    return fmt::format("{} = fir_apply({}, {}, {})", io.out[0], io.in[0], *h, flag(io, em, "compensate", "yes"));
}

core::Result<std::string> savgol(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto deriv = em.expr(io, "deriv");
    const auto delta = uses_variable(io, "deriv") ? fmt::format("(1 / {} if {} > 0 else 1.0)", io.in_fs[0], deriv) : param_int(p, "deriv") > 0 ? "1 / " + io.in_fs[0] : std::string{"1.0"};
    return fmt::format("{} = signal.savgol_filter({}, {}, {}, deriv={}, delta={}, mode={}, axis=1)", io.out[0], io.in[0], em.expr(io, "length"), em.expr(io, "polyorder"), deriv, delta,
                       em.expr(io, "mode"));
}

core::Result<std::string> hilbert(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    const auto z = fmt::format("signal.hilbert({}, axis=1)", io.in[0]);
    if (uses_variable(io, "output")) {
        const auto o = em.expr(io, "output");
        return fmt::format("{0}_v = {1}\n{0} = np.imag({0}_v) if {2} == \"imag\" else np.angle({0}_v) if {2} == \"phase\" else np.unwrap(np.angle({0}_v), axis=1)", io.out[0], z, o);
    }
    const auto& output = param_text(io.node.params, "output");
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

core::Result<std::string> edge_list(const NodeIo& io, Emitter& em, const std::string& scale) {
    if (uses_variable(io, "bands") || param_text(io.node.params, "bands").find("nyquist") != std::string::npos) {
        em.function("number_list");
        return fmt::format("[f / {} for f in number_list({}, {} / 2)]", scale, em.expr(io, "bands"), io.in_fs[0]);
    }
    const auto bands = band_edges(io);
    if (!bands) return std::unexpected{bands.error()};
    std::string edges = "[";
    for (std::size_t k = 0; k < bands->size(); ++k) edges += (k ? ", " : "") + em.number((*bands)[k]) + " / " + scale;
    return edges + "]";
}

std::string weight_arg(const NodeIo& io, Emitter& em) {
    if (uses_variable(io, "weight")) {
        em.function("number_list");
        return fmt::format("(number_list({0}) if str({0}).strip(\" ,\") else None)", em.expr(io, "weight"));
    }
    const auto& text = param_text(io.node.params, "weight");
    if (text.find_first_not_of(" ,") == std::string::npos) return "None";
    const auto w = pipeline::blocks::parse_numbers(io.node.type->id, "weight", text);
    return w ? list(em, "weight", *w) : std::string{"None"};
}

core::Result<std::string> remez(const NodeIo& io, Emitter& em) {
    const auto edges = edge_list(io, em, io.in_fs[0]);
    const auto desired = number_list(io, em, "desired");
    if (!edges) return std::unexpected{edges.error()};
    if (!desired) return std::unexpected{desired.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    return fmt::format("{0}_h = signal.remez({1}, {2}, {3}, weight={4}, type={5})\n{0} = fir_apply({6}, {0}_h, {7})", io.out[0], em.expr(io, "numtaps"), *edges, *desired,
                       weight_arg(io, em), em.expr(io, "type"), io.in[0], flag(io, em, "compensate", "yes"));
}

core::Result<std::string> firls(const NodeIo& io, Emitter& em) {
    const auto edges = edge_list(io, em, "(" + io.in_fs[0] + " / 2)");
    const auto desired = number_list(io, em, "desired");
    if (!edges) return std::unexpected{edges.error()};
    if (!desired) return std::unexpected{desired.error()};
    em.import_line("from scipy import signal");
    em.function("fir_apply");
    return fmt::format("{0}_h = signal.firls({1}, {2}, {3}, weight={4})\n{0} = fir_apply({5}, {0}_h, {6})", io.out[0], em.expr(io, "numtaps"), *edges, *desired, weight_arg(io, em),
                       io.in[0], flag(io, em, "compensate", "yes"));
}

const pipeline::Coeffs* coeffs_value(const pipeline::Value* v) { return v ? std::get_if<pipeline::Coeffs>(v) : nullptr; }

std::size_t levels_of(const NodeIo& io, bool input) {
    const auto* c = coeffs_value(input ? io.in_values[0] : io.out_values[0]);
    return c ? c->levels() : 3;
}

core::Result<std::string> downsample(const NodeIo& io, Emitter& em) {
    em.function("multirate");
    const auto m = em.expr(io, "m");
    return fmt::format("{0} = multirate({1}[:, {2}:], 1, {3}, {4}, {5})\n{0}_fs = {6} / {3}", io.out[0], io.in[0], em.expr(io, "phase"), m, em.expr(io, "attenuation_db"),
                       flag(io, em, "anti_alias", "yes"), io.in_fs[0]);
}

core::Result<std::string> upsample(const NodeIo& io, Emitter& em) {
    em.function("multirate");
    const auto m = em.expr(io, "m");
    return fmt::format("{0} = multirate({1}, {2}, 1, {3}, {4})\n{0}_fs = {5} * {2}", io.out[0], io.in[0], m, em.expr(io, "attenuation_db"), flag(io, em, "anti_image", "yes"), io.in_fs[0]);
}

core::Result<std::string> resample(const NodeIo& io, Emitter& em) {
    em.function("multirate");
    const auto l = em.expr(io, "l"), m = em.expr(io, "m");
    return fmt::format("{0} = multirate({1}, {2}, {3}, {4}, True)\n{0}_fs = {5} * {2} / {3}", io.out[0], io.in[0], l, m, em.expr(io, "attenuation_db"), io.in_fs[0]);
}

core::Result<std::string> convolve(const NodeIo& io, Emitter& em) {
    const auto& spec = param_text(io.node.params, "filter");
    std::vector<double> h;
    const auto dot = spec.rfind('.');
    if (dot != std::string::npos)
        if (const auto bank = core::wavelet(spec.substr(0, dot))) {
            const auto role = spec.substr(dot + 1);
            const auto* f = role == "h0" ? &bank->h0 : role == "h1" ? &bank->h1 : role == "f0" ? &bank->f0 : role == "f1" ? &bank->f1 : nullptr;
            if (f) h = f->coeffs;
        }
    if (h.empty())
        if (const auto mine = core::named_filter(spec)) h = mine->coeffs;
    if (h.empty()) return std::unexpected{unsupported(io, "filter")};
    auto taps = em.numbers("h", h);
    if (uses_variable(io, "filter")) {
        em.need_wavelets(dot == std::string::npos ? spec : spec.substr(0, dot));
        em.function("filter_taps");
        taps = "filter_taps(" + em.expr(io, "filter") + ")";
    }
    return fmt::format("{0}_h = {1}\n{0} = np.stack([np.convolve(row, {0}_h) for row in {2}])", io.out[0], taps, io.in[0]);
}

std::string auto_levels(const NodeIo& io, Emitter& em, const std::string& w, std::size_t actual) {
    if (uses_variable(io, "levels")) {
        em.function("default_levels");
        return fmt::format("({} or default_levels({}.shape[1], {}))", em.expr(io, "levels"), io.in[0], w);
    }
    if (param_int(io.node.params, "levels") == 0) {
        em.function("default_levels");
        return fmt::format("default_levels({}.shape[1], {})", io.in[0], w);
    }
    return std::to_string(actual);
}

core::Result<std::string> dwt(const NodeIo& io, Emitter& em) {
    const auto w = em.wavelet_of(io, "wavelet");
    const auto mode = em.expr(io, "ext");
    em.import_line("import pywt");
    return fmt::format("{0} = pywt.wavedec({1}, {2}, mode={3}, level={4}, axis=-1)\n"
                       "{0}_meta = {{\"transform\": \"dwt\", \"wavelet\": {2}, \"mode\": {3}, \"length\": {1}.shape[1], \"rate\": {5}, \"norm\": False}}",
                       io.out[0], io.in[0], w, mode, auto_levels(io, em, w, levels_of(io, false)), io.in_fs[0]);
}

core::Result<std::string> idwt(const NodeIo& io, Emitter& em) {
    const auto& c = io.in[0];
    const auto own = c + "_meta[\"mode\"]";
    const auto mode = uses_variable(io, "ext") ? fmt::format("({} if {} == \"auto\" else {})", own, em.expr(io, "ext"), em.expr(io, "ext"))
                      : param_text(io.node.params, "ext") == "auto"                        ? own
                                                                                            : em.expr(io, "ext");
    em.import_line("import pywt");
    return fmt::format("{0} = pywt.waverec({1}, {1}_meta[\"wavelet\"], mode={2}, axis=-1)[:, :{1}_meta[\"length\"]]\n{0}_fs = {1}_meta[\"rate\"]", io.out[0], c, mode);
}

core::Result<std::string> swt(const NodeIo& io, Emitter& em) {
    const auto w = em.wavelet_of(io, "wavelet");
    const auto levels = auto_levels(io, em, w, levels_of(io, false));
    const auto norm = uses_variable(io, "norm") ? "(" + em.expr(io, "norm") + " == \"yes\")" : std::string{param_text(io.node.params, "norm") == "yes" ? "True" : "False"};
    em.import_line("import pywt");
    return fmt::format("{0}_block = 2 ** {1}\n{0}_size = -(-{2}.shape[1] // {0}_block) * {0}_block\n"
                       "{0} = pywt.swt({2}[:, np.arange({0}_size) % {2}.shape[1]], {3}, level={1}, trim_approx=True, norm={4}, axis=-1)\n"
                       "{0}_meta = {{\"transform\": \"swt\", \"wavelet\": {3}, \"mode\": \"periodization\", \"length\": {2}.shape[1], \"rate\": {5}, \"norm\": {4}}}",
                       io.out[0], levels, io.in[0], w, norm, io.in_fs[0]);
}

core::Result<std::string> iswt(const NodeIo& io, Emitter& em) {
    em.import_line("import pywt");
    return fmt::format("{0} = pywt.iswt({1}, {1}_meta[\"wavelet\"], norm={1}_meta[\"norm\"], axis=-1)[:, :{1}_meta[\"length\"]]\n{0}_fs = {1}_meta[\"rate\"]", io.out[0], io.in[0]);
}

core::Result<std::string> threshold(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& c = io.in[0];
    const auto norm = fmt::format("{0}_meta[\"transform\"] == \"swt\" and {0}_meta[\"norm\"]", c);
    std::string first, last;
    if (uses_variable(io, "levels")) {
        em.function("level_range");
        first = "level_range(" + em.expr(io, "levels") + ")[0]";
        last = "level_range(" + em.expr(io, "levels") + ")[1]";
    } else {
        const auto [a, b] = pipeline::param_range(p, "levels");
        first = std::to_string(a);
        last = std::to_string(b);
    }
    std::string sigma = "None";
    if (uses_variable(io, "sigma")) em.function("threshold_sigma"), sigma = fmt::format("threshold_sigma({}, {})", em.expr(io, "sigma"), norm);
    else if (param_text(p, "sigma") != "mad_d1") sigma = fmt::format("{} * (np.sqrt(0.5) if {} else 1.0)", em.number(param_real(p, "sigma")), norm);
    em.function("shrink_coeffs");
    em.import_line("import pywt");
    return fmt::format("{0} = shrink_coeffs({1}, {2}, {3}, {4}, {5}, {6}, {1}_meta[\"length\"], {7})\n{0}_meta = {1}_meta", io.out[0], c, em.expr(io, "rule"), em.expr(io, "mode"), first,
                       last, sigma, norm);
}

core::Result<std::string> band_edit(const NodeIo& io, Emitter& em) {
    em.function("band_gains");
    return fmt::format("{0}_gains = band_gains({1}, {1}_meta, {2}, {3})\n{0} = [np.zeros_like(band) if g is None else band * g for band, g in zip({1}, {0}_gains)]\n{0}_meta = {1}_meta",
                       io.out[0], io.in[0], em.expr(io, "enabled"), em.expr(io, "gains_db"));
}

core::Result<std::string> band_energy(const NodeIo& io, Emitter& em) {
    em.function("band_energy");
    em.function("band_labels");
    em.function("swt_weights");
    return fmt::format("{0} = band_energy({1}, band_labels({1}, {1}_meta), swt_weights({1}, {1}_meta))\n{0}_ok = True", io.out[0], io.in[0]);
}

core::Result<std::string> envelope(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    return fmt::format("{} = np.abs(signal.hilbert({}, axis=1))", io.out[0], io.in[0]);
}

core::Result<std::string> inst_freq(const NodeIo& io, Emitter& em) {
    em.function("inst_freq");
    return fmt::format("{} = inst_freq({}, {})", io.out[0], io.in[0], io.in_fs[0]);
}

core::Result<std::string> cepstrum(const NodeIo& io, Emitter& em) {
    em.function("real_cepstrum");
    return fmt::format("{} = real_cepstrum({})", io.out[0], io.in[0]);
}

core::Result<std::string> dct(const NodeIo& io, Emitter& em, int type) {
    em.import_line("import scipy.fft");
    return fmt::format("{} = scipy.fft.dct({}, type={}, norm=\"ortho\", axis=1)", io.out[0], io.in[0], type);
}

core::Result<std::string> xcorr(const NodeIo& io, Emitter& em) {
    em.function("xcorr");
    return fmt::format("{} = xcorr({}, {}, {}, {})", io.out[0], io.in[0], io.in[1], em.expr(io, "mode"), flag(io, em, "normalize", "yes"));
}

core::Result<std::string> denoise(const NodeIo& io, Emitter& em) {
    const auto w = em.wavelet_of(io, "wavelet");
    std::string levels = em.expr(io, "levels");
    if (uses_variable(io, "levels") || param_int(io.node.params, "levels") == 0) levels = auto_levels(io, em, w, 0);
    em.function("denoise");
    em.import_line("import pywt");
    return fmt::format("{} = denoise({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], w, levels, em.expr(io, "rule"), em.expr(io, "mode"), em.expr(io, "shifts"));
}

core::Result<std::string> thd(const NodeIo& io, Emitter& em) {
    em.function("thd");
    return fmt::format("{0} = thd({1}, {2}, {3})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], em.expr(io, "harmonics"));
}

core::Result<std::string> onsets(const NodeIo& io, Emitter& em) {
    em.function("onsets");
    const auto samples = [&](std::string_view param) {
        if (!uses_variable(io, param)) return std::to_string(param_int(io.node.params, param));
        em.function("as_samples");
        return "as_samples(" + em.expr(io, param) + ", " + io.in_fs[0] + ")";
    };
    return fmt::format("{0} = onsets({1}, {2}, {3}, {4})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], samples("frame"), samples("hop"));
}

std::filesystem::path out_file(const NodeIo& io) {
    auto path = pipeline::utf8_path(param_text(io.node.params, "path"));
    return path.is_relative() ? io.base_dir / path : path;
}

core::Result<std::string> export_signal(const NodeIo& io, Emitter& em) {
    em.function("export_signal");
    return fmt::format("{0} = export_signal({1}, {2}, {3}, {4})\n{0}_ok = True", io.out[0], em.file(out_file(io)), io.in[0], io.in_fs[0], em.expr(io, "format"));
}

core::Result<std::string> export_report(const NodeIo& io, Emitter& em) {
    em.function("export_report_file");
    return fmt::format("{0} = export_report_file({1}, {2}, {2}_ok, {3})\n{0}_ok = True", io.out[0], em.file(out_file(io)), io.in[0], em.expr(io, "format"));
}

core::Result<std::string> check(const NodeIo& io, Emitter& em) {
    em.function("check_signals");
    return fmt::format("{0}, {0}_ok = check_signals({1}, {2}, {3}, {4}, {5})", io.out[0], io.in[0], io.in[1], em.expr(io, "kind"), em.expr(io, "tol"), em.expr(io, "delay"));
}

core::Result<std::string> listen(const NodeIo& io, Emitter& em) {
    if (!em.notebook()) return fmt::format("print({}, {})", py_str(tr("python.listen")), py_str(io.node.id));
    em.import_line("from IPython.display import Audio, display");
    return fmt::format("display(Audio({}, rate={}))", io.in[0], io.in_fs[0]);
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
    out.push_back({"inst_freq", 1e-7, inst_freq});
    out.push_back({"remez", 1e-8, remez});
    out.push_back({"firls", 1e-10, firls});
    out.push_back({"cepstrum", 1e-9, cepstrum});
    out.push_back({"dct", 1e-12, [](const NodeIo& io, Emitter& em) { return dct(io, em, 2); }});
    out.push_back({"idct", 1e-12, [](const NodeIo& io, Emitter& em) { return dct(io, em, 3); }});
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"downsample", downsample}, {"upsample", upsample}, {"resample", resample}, {"convolve", convolve}, {"dwt", dwt}, {"idwt", idwt}, {"swt", swt}, {"iswt", iswt}, {"threshold", threshold}, {"band_edit", band_edit}, {"band_energy", band_energy},
             {"envelope", envelope}, {"xcorr", xcorr}, {"denoise", denoise}, {"thd", thd}, {"onsets", onsets}, {"export", export_signal},
             {"export_report", export_report}, {"check", check}, {"listen", listen}})
        out.push_back({id, 1e-12, fn});
}

}  // namespace signals::python
