#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/dtcwt.hpp"
#include "core/extension.hpp"
#include "core/lifting.hpp"
#include "core/packets.hpp"
#include "io/npy.hpp"
#include "pipeline/npz_values.hpp"
#include "python/form.hpp"

namespace signals::python {
namespace {

using pipeline::param_int;
using pipeline::param_real;
using pipeline::param_text;

const pipeline::Coeffs* coeffs_of(const pipeline::Value* v) { return v ? std::get_if<pipeline::Coeffs>(v) : nullptr; }

std::string constants97(Emitter& em) {
    const auto& c = core::lift97_constants();
    return "[" + em.number(c.alpha) + ", " + em.number(c.beta) + ", " + em.number(c.gamma) + ", " + em.number(c.delta) + ", " + em.number(c.zeta) + "]";
}

std::string paths_of(const pipeline::Coeffs& c) {
    std::string out = "[";
    for (std::size_t k = 0; k < c.basis.size(); ++k) out += (k ? ", " : "") + py_str(core::packet_path(c.basis[k]));
    return out + "]";
}

std::size_t length_of(const pipeline::Coeffs* c) { return c && !c->lengths.empty() ? c->lengths.front() : 0; }

void put_dtcwt_filters(Emitter& em, const std::string& biort, const std::string& qshift) {
    if (const auto* b = core::dtcwt::find_biort(biort)) {
        const std::pair<const char*, const std::vector<double>*> parts[] = {{"h0o", &b->h0o}, {"g0o", &b->g0o}, {"h1o", &b->h1o}, {"g1o", &b->g1o}};
        for (const auto& [k, v] : parts) em.put("dtcwt." + biort + "." + k, io::np_vector(*v));
    }
    if (const auto* q = core::dtcwt::find_qshift(qshift)) {
        const std::pair<const char*, const std::vector<double>*> parts[] = {{"h0a", &q->h0a}, {"h0b", &q->h0b}, {"g0a", &q->g0a}, {"g0b", &q->g0b},
                                                                            {"h1a", &q->h1a}, {"h1b", &q->h1b}, {"g1a", &q->g1a}, {"g1b", &q->g1b}};
        for (const auto& [k, v] : parts) em.put("dtcwt." + qshift + "." + k, io::np_vector(*v));
    }
}

core::Result<std::string> dtcwt(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& b = param_text(p, "biort");
    const auto& q = param_text(p, "qshift");
    put_dtcwt_filters(em, b, q);
    em.function("dtcwt");
    return fmt::format("{} = dtcwt({}, {}, {}, {}, D)", io.out[0], io.in[0], param_int(p, "levels"), py_str(b), py_str(q));
}

core::Result<std::string> idtcwt(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.in_values[0]);
    const std::string w = c ? c->wavelet : std::string{"near_sym_a+qshift_a"};
    const auto plus = w.find('+');
    put_dtcwt_filters(em, w.substr(0, plus), w.substr(plus + 1));
    em.function("dtcwt");
    return fmt::format("{} = idtcwt({}, {}, {}, D, {})", io.out[0], io.in[0], py_str(w.substr(0, plus)), py_str(w.substr(plus + 1)), length_of(c));
}

core::Result<std::string> lift53(const NodeIo& io, Emitter& em) {
    em.function("lift");
    const bool integer = param_text(io.node.params, "integer") == "yes";
    return fmt::format("{} = lift({}, {}, {})", io.out[0], io.in[0], param_int(io.node.params, "levels"), integer ? "\"lift53i\"" : "\"lift53\"");
}

core::Result<std::string> lift97(const NodeIo& io, Emitter& em) {
    em.function("lift");
    return fmt::format("{} = lift({}, {}, \"lift97\", {})", io.out[0], io.in[0], param_int(io.node.params, "levels"), constants97(em));
}

core::Result<std::string> ilift(const NodeIo& io, Emitter& em) {
    em.function("lift");
    const auto* c = coeffs_of(io.in_values[0]);
    const std::string kind = c ? c->transform : io.node.type->id == "ilift97" ? std::string{"lift97"} : std::string{"lift53"};
    return fmt::format("{} = unlift({}, {}{})", io.out[0], io.in[0], py_str(kind), kind == "lift97" ? ", " + constants97(em) : std::string{});
}

core::Result<std::string> wpt(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& basis = param_text(p, "basis");
    const auto* c = coeffs_of(io.out_values[0]);
    em.function("wpt");
    em.import_line("import pywt");
    const auto which = basis == "best" || !c ? py_str(basis) : paths_of(*c);
    return fmt::format("{0}, {0}_paths = wpt({1}, {2}, {3}, {4}, {5})", io.out[0], io.in[0], em.wavelet(param_text(p, "wavelet")), py_str(param_text(p, "ext")),
                       param_int(p, "levels"), which);
}

core::Result<std::string> iwpt(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.in_values[0]);
    em.function("wpt");
    em.import_line("import pywt");
    return fmt::format("{} = iwpt({}, {}, {}, {}, {})", io.out[0], io.in[0], c ? paths_of(*c) : io.in[0] + "_paths", em.wavelet(c ? c->wavelet : std::string{"db2"}),
                       py_str(c ? std::string{core::to_string(c->mode)} : std::string{"symmetric"}), length_of(c));
}

core::Result<std::string> keep_largest(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    em.function("keep_largest");
    return fmt::format("{} = keep_largest({}, {}, {}, {})", io.out[0], io.in[0], param_text(p, "mode") == "percent" ? "True" : "False", em.number(param_real(p, "amount")),
                       param_text(p, "keep_approx") == "yes" ? 1 : 0);
}

core::Result<std::string> coeff_quantize(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    em.function("coeff_quantize");
    return fmt::format("{} = coeff_quantize({}, {}, {}, {})", io.out[0], io.in[0], param_text(p, "mode") == "bits" ? "True" : "False", param_int(p, "bits"),
                       em.number(param_real(p, "step")));
}

core::Result<std::string> polyphase(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = {2}[:, 0::2]\n{1} = {2}[:, 1::2]", io.out[0], io.out[1], io.in[0]);
}

core::Result<std::string> ipolyphase(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = np.zeros(({1}.shape[0], {1}.shape[1] + {2}.shape[1]))\n{0}[:, 0::2] = {1}\n{0}[:, 1::2] = {2}", io.out[0], io.in[0], io.in[1]);
}

std::string inverse_expr(const pipeline::Coeffs& c, const std::string& var, Emitter& em) {
    const auto n = length_of(&c);
    if (c.transform == "dwt") return fmt::format("pywt.waverec({}, {}, mode={}, axis=-1)[:, :{}]", var, em.wavelet(c.wavelet), py_str(std::string{core::to_string(c.mode)}), n);
    if (c.transform == "swt") return fmt::format("pywt.iswt({}, {}, norm={}, axis=-1)[:, :{}]", var, em.wavelet(c.wavelet), c.norm ? "True" : "False", n);
    if (c.transform == "wpt") {
        em.function("wpt");
        return fmt::format("iwpt({}, {}, {}, {}, {})", var, paths_of(c), em.wavelet(c.wavelet), py_str(std::string{core::to_string(c.mode)}), n);
    }
    if (c.transform == "dtcwt") {
        em.function("dtcwt");
        const auto plus = c.wavelet.find('+');
        put_dtcwt_filters(em, c.wavelet.substr(0, plus), c.wavelet.substr(plus + 1));
        return fmt::format("idtcwt({}, {}, {}, D, {})", var, py_str(c.wavelet.substr(0, plus)), py_str(c.wavelet.substr(plus + 1)), n);
    }
    em.function("lift");
    return fmt::format("unlift({}, {}{})", var, py_str(c.transform), c.transform == "lift97" ? ", " + constants97(em) : std::string{});
}

core::Result<std::string> mra(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.in_values[0]);
    if (!c) return fmt::format("{} = {}", io.out[0], io.in[0]);
    const auto keep = pipeline::band_index(*c, param_text(io.node.params, "component"));
    if (!keep) return std::unexpected{unsupported(io, "component")};
    em.import_line("import pywt");
    return fmt::format("{0}_only = [band if k == {1} else np.zeros_like(band) for k, band in enumerate({2})]\n{0} = {3}", io.out[0], *keep, io.in[0],
                       inverse_expr(*c, io.out[0] + "_only", em));
}

core::Result<std::string> coeffs_file(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.out_values[0]);
    if (!c) return fmt::format("{} = []", io.out[0]);
    auto path = pipeline::utf8_path(param_text(io.node.params, "path"));
    if (path.is_relative()) path = io.base_dir / path;
    const auto keys = pipeline::band_keys(*c);
    std::string bands;
    for (std::size_t k = 0; k < keys.size(); ++k) {
        std::string band = fmt::format("np.atleast_2d({0}_f[{1}])", io.out[0], py_str(keys[k]));
        if (c->transform == "dtcwt" && k > 0)
            band = fmt::format("np.stack((np.atleast_2d({0}_f[{1}]), np.atleast_2d({0}_f[{2}])), axis=-1).reshape(np.atleast_2d({0}_f[{1}]).shape[0], -1)", io.out[0],
                               py_str("re_" + keys[k]), py_str("im_" + keys[k]));
        bands += (k ? ", " : "") + band;
    }
    std::string text = fmt::format("{0}_f = np.load({1}, allow_pickle=False)\n{0} = [{2}]", io.out[0], em.file(path), bands);
    if (c->transform == "dwt" || c->transform == "swt")
        text += fmt::format("\nif \"detail_sign\" in {0}_f and str({0}_f[\"detail_sign\"]) == \"signals\":\n    {0} = [{0}[0]] + [-b for b in {0}[1:]]", io.out[0]);
    if (c->transform == "wpt") text += fmt::format("\n{}_paths = {}", io.out[0], paths_of(*c));
    return text;
}

}  // namespace

void register_wavelet_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"lift53", lift53}, {"lift97", lift97}, {"ilift53", ilift}, {"ilift97", ilift}, {"wpt", wpt}, {"iwpt", iwpt}, {"keep_largest", keep_largest},
             {"coeff_quantize", coeff_quantize}, {"polyphase", polyphase}, {"ipolyphase", ipolyphase}, {"mra", mra}, {"coeffs_file", coeffs_file}, {"dtcwt", dtcwt}, {"idtcwt", idtcwt}})
        out.push_back({id, 1e-12, fn});
}

}  // namespace signals::python
