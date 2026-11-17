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

void put_all_dtcwt_filters(const NodeIo& io, Emitter& em) {
    const auto choices = [&](std::string_view name) {
        for (const auto& s : io.node.type->params)
            if (s.name == name) return s.choices;
        return std::vector<std::string>{};
    };
    const auto& p = io.node.params;
    const auto biorts = uses_variable(io, "biort") ? choices("biort") : std::vector<std::string>{param_text(p, "biort")};
    const auto qshifts = uses_variable(io, "qshift") ? choices("qshift") : std::vector<std::string>{param_text(p, "qshift")};
    for (const auto& b : biorts)
        for (const auto& q : qshifts) put_dtcwt_filters(em, b, q);
}

core::Result<std::string> dtcwt(const NodeIo& io, Emitter& em) {
    put_all_dtcwt_filters(io, em);
    em.function("dtcwt");
    return fmt::format("{0} = dtcwt({1}, {2}, {3}, {4}, D)\n{0}_meta = {{\"transform\": \"dtcwt\", \"biort\": {3}, \"qshift\": {4}, \"length\": {1}.shape[1], \"rate\": {5}, \"norm\": False}}",
                       io.out[0], io.in[0], em.expr(io, "levels"), em.expr(io, "biort"), em.expr(io, "qshift"), io.in_fs[0]);
}

core::Result<std::string> idtcwt(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.in_values[0]);
    const std::string w = c ? c->wavelet : std::string{"near_sym_a+qshift_a"};
    const auto plus = w.find('+');
    put_dtcwt_filters(em, w.substr(0, plus), w.substr(plus + 1));
    em.function("dtcwt");
    return fmt::format("{0} = idtcwt({1}, {1}_meta[\"biort\"], {1}_meta[\"qshift\"], D, {1}_meta[\"length\"])\n{0}_fs = {1}_meta[\"rate\"]", io.out[0], io.in[0]);
}

core::Result<std::string> lift53(const NodeIo& io, Emitter& em) {
    em.function("lift");
    const auto kind = uses_variable(io, "integer") ? "(\"lift53i\" if " + em.expr(io, "integer") + " == \"yes\" else \"lift53\")"
                                                   : std::string{param_text(io.node.params, "integer") == "yes" ? "\"lift53i\"" : "\"lift53\""};
    return fmt::format("{0} = lift({1}, {2}, {3})\n{0}_meta = {{\"transform\": {3}, \"length\": {1}.shape[1], \"rate\": {4}, \"norm\": False}}", io.out[0], io.in[0],
                       em.expr(io, "levels"), kind, io.in_fs[0]);
}

core::Result<std::string> lift97(const NodeIo& io, Emitter& em) {
    em.function("lift");
    return fmt::format("{0} = lift({1}, {2}, \"lift97\", {3})\n{0}_meta = {{\"transform\": \"lift97\", \"length\": {1}.shape[1], \"rate\": {4}, \"norm\": False}}", io.out[0], io.in[0],
                       em.expr(io, "levels"), constants97(em), io.in_fs[0]);
}

core::Result<std::string> ilift(const NodeIo& io, Emitter& em) {
    em.function("lift");
    const auto* c = coeffs_of(io.in_values[0]);
    const bool k97 = c ? c->transform == "lift97" : io.node.type->id == "ilift97";
    return fmt::format("{0} = unlift({1}, {1}_meta[\"transform\"]{2})\n{0}_fs = {1}_meta[\"rate\"]", io.out[0], io.in[0], k97 ? ", " + constants97(em) : std::string{});
}

core::Result<std::string> wpt(const NodeIo& io, Emitter& em) {
    const auto w = em.wavelet_of(io, "wavelet");
    const auto mode = em.expr(io, "ext");
    em.function("wpt");
    em.import_line("import pywt");
    return fmt::format("{0}, {0}_paths = wpt({1}, {2}, {3}, {4}, {5})\n"
                       "{0}_meta = {{\"transform\": \"wpt\", \"wavelet\": {2}, \"mode\": {3}, \"length\": {1}.shape[1], \"rate\": {6}, \"norm\": False, \"paths\": {0}_paths}}",
                       io.out[0], io.in[0], w, mode, em.expr(io, "levels"), em.expr(io, "basis"), io.in_fs[0]);
}

core::Result<std::string> iwpt(const NodeIo& io, Emitter& em) {
    em.function("wpt");
    em.import_line("import pywt");
    return fmt::format("{0} = iwpt({1}, {1}_meta[\"paths\"], {1}_meta[\"wavelet\"], {1}_meta[\"mode\"], {1}_meta[\"length\"])\n{0}_fs = {1}_meta[\"rate\"]", io.out[0], io.in[0]);
}

core::Result<std::string> keep_largest(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    em.function("keep_largest");
    const auto percent = uses_variable(io, "mode") ? "(" + em.expr(io, "mode") + " == \"percent\")" : std::string{param_text(p, "mode") == "percent" ? "True" : "False"};
    const auto approx = uses_variable(io, "keep_approx") ? "int(" + em.expr(io, "keep_approx") + " == \"yes\")" : std::string{param_text(p, "keep_approx") == "yes" ? "1" : "0"};
    return fmt::format("{0} = keep_largest({1}, {2}, {3}, {4})\n{0}_meta = {1}_meta", io.out[0], io.in[0], percent, em.expr(io, "amount"), approx);
}

core::Result<std::string> coeff_quantize(const NodeIo& io, Emitter& em) {
    em.function("coeff_quantize");
    const auto bits = uses_variable(io, "mode") ? "(" + em.expr(io, "mode") + " == \"bits\")" : std::string{param_text(io.node.params, "mode") == "bits" ? "True" : "False"};
    return fmt::format("{0} = coeff_quantize({1}, {2}, {3}, {4})\n{0}_meta = {1}_meta", io.out[0], io.in[0], bits, em.expr(io, "bits"), em.expr(io, "step"));
}

core::Result<std::string> polyphase(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = {2}[:, 0::2]\n{1} = {2}[:, 1::2]", io.out[0], io.out[1], io.in[0]);
}

core::Result<std::string> ipolyphase(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = np.zeros(({1}.shape[0], {1}.shape[1] + {2}.shape[1]))\n{0}[:, 0::2] = {1}\n{0}[:, 1::2] = {2}", io.out[0], io.in[0], io.in[1]);
}

std::string inverse_expr(const pipeline::Coeffs& c, const std::string& var, const std::string& meta, Emitter& em) {
    const auto at = [&](const char* key) { return meta + "[\"" + key + "\"]"; };
    if (c.transform == "dwt") return fmt::format("pywt.waverec({}, {}, mode={}, axis=-1)[:, :{}]", var, at("wavelet"), at("mode"), at("length"));
    if (c.transform == "swt") return fmt::format("pywt.iswt({}, {}, norm={}, axis=-1)[:, :{}]", var, at("wavelet"), at("norm"), at("length"));
    if (c.transform == "wpt") {
        em.function("wpt");
        return fmt::format("iwpt({}, {}, {}, {}, {})", var, at("paths"), at("wavelet"), at("mode"), at("length"));
    }
    if (c.transform == "dtcwt") {
        em.function("dtcwt");
        const auto plus = c.wavelet.find('+');
        put_dtcwt_filters(em, c.wavelet.substr(0, plus), c.wavelet.substr(plus + 1));
        return fmt::format("idtcwt({}, {}, {}, D, {})", var, at("biort"), at("qshift"), at("length"));
    }
    em.function("lift");
    return fmt::format("unlift({}, {}{})", var, at("transform"), c.transform == "lift97" ? ", " + constants97(em) : std::string{});
}

core::Result<std::string> mra(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.in_values[0]);
    if (!c) return fmt::format("{} = {}", io.out[0], io.in[0]);
    em.import_line("import pywt");
    em.function("band_index");
    return fmt::format("{0}_only = [band if k == band_index({2}, {2}_meta, {1}) else np.zeros_like(band) for k, band in enumerate({2})]\n{0} = {3}\n{0}_fs = {2}_meta[\"rate\"]", io.out[0],
                       em.expr(io, "component"), io.in[0], inverse_expr(*c, io.out[0] + "_only", io.in[0] + "_meta", em));
}

core::Result<std::string> coeffs_file(const NodeIo& io, Emitter& em) {
    const auto* c = coeffs_of(io.out_values[0]);
    if (!c) return fmt::format("{0} = []\n{0}_meta = None", io.out[0]);
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
    const auto from_file = [&](std::string_view param, const std::string& value) {
        if (uses_variable(io, param)) return "(" + em.expr(io, param) + " or " + value + ")";
        return value;
    };
    std::string wavelet = c->transform == "dtcwt" ? py_str(c->wavelet) : em.wavelet(c->wavelet);
    if (uses_variable(io, "wavelet") && c->transform != "dtcwt") {
        (void)em.wavelet_of(io, "wavelet");
        wavelet = "wavelet(" + em.expr(io, "wavelet") + " or " + py_str(c->wavelet) + ")";
    }
    std::string text = fmt::format("{0}_f = np.load({1}, allow_pickle=False)\n{0} = [{2}]", io.out[0], em.file(path), bands);
    if (c->transform == "dwt" || c->transform == "swt")
        text += fmt::format("\nif \"detail_sign\" in {0}_f and str({0}_f[\"detail_sign\"]) == \"signals\":\n    {0} = [{0}[0]] + [-b for b in {0}[1:]]", io.out[0]);
    if (c->transform == "wpt") text += fmt::format("\n{}_paths = {}", io.out[0], paths_of(*c));
    const auto plus = c->wavelet.find('+');
    const auto dtcwt_names = c->transform == "dtcwt" && plus != std::string::npos
                                 ? fmt::format(", \"biort\": {}, \"qshift\": {}", py_str(c->wavelet.substr(0, plus)), py_str(c->wavelet.substr(plus + 1)))
                                 : std::string{};
    text += fmt::format("\n{0}_meta = {{\"transform\": {1}, \"wavelet\": {2}, \"mode\": {3}, \"length\": {4}, \"rate\": {5}, \"norm\": {6}{7}{8}}}", io.out[0], py_str(c->transform), wavelet,
                        from_file("mode", py_str(std::string{core::to_string(c->mode)})), length_of(c), from_file("rate", em.number(c->rate)), c->norm ? "True" : "False",
                        c->transform == "wpt" ? ", \"paths\": " + io.out[0] + "_paths" : std::string{}, dtcwt_names);
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
