#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/expr.hpp"
#include "core/swt.hpp"
#include "pipeline/blocks_common.hpp"
#include "python/form.hpp"

namespace signals::python {
namespace {

using pipeline::param_int;
using pipeline::param_real;
using pipeline::param_text;

double input_rate(const NodeIo& io, std::size_t k = 0) {
    if (k < io.in_values.size() && io.in_values[k])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[k])) return a->sample_rate;
    return 44100;
}

core::Result<std::string> stats(const NodeIo& io, Emitter& em) {
    em.function("stats");
    return fmt::format("{0} = stats({1}, {2})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0]);
}

core::Result<std::string> loudness(const NodeIo& io, Emitter& em) {
    const auto fs = input_rate(io);
    const auto window = pipeline::param_samples(io.node.params, "window", fs), hop = pipeline::param_samples(io.node.params, "hop", fs);
    if (!window || !hop || *window == 0 || *hop == 0) return std::unexpected{unsupported(io, "window")};
    em.function("loudness");
    return fmt::format("{0} = loudness({1}, {2}, {3})\n{0}_ok = True", io.out[0], io.in[0], *window, *hop);
}

struct PyExpr {
    std::string text;
    int prec = 100;
};

std::string wrap(const PyExpr& e, int at_least) { return e.prec >= at_least ? e.text : "(" + e.text + ")"; }

class AssertTranslator {
public:
    AssertTranslator(const core::Expr& e, const NodeIo& io, const Emitter& em) : e_{e}, io_{io}, em_{em} {}

    PyExpr value(std::size_t at) const {
        using Op = core::Expr::Op;
        const auto& n = e_.nodes[at];
        const auto v = [&](std::size_t k, int prec) { return wrap(value(n.args[k]), prec); };
        switch (n.op) {
        case Op::number: {
            auto text = em_.number(n.value);
            if (text.find_first_not_of("-0123456789") == std::string::npos) text += ".0";
            return {text};
        }
        case Op::name: {
            const auto [var, field] = name(n.name);
            if (field == "passed") return {"float(" + var + "_ok)"};
            return {var + "[" + py_str(field) + "]"};
        }
        case Op::neg: return {"-" + v(0, 80), 80};
        case Op::plus: return value(n.args[0]);
        case Op::add: return {v(0, 60) + " + " + v(1, 61), 60};
        case Op::sub: return {v(0, 60) + " - " + v(1, 61), 60};
        case Op::mul: return {v(0, 70) + " * " + v(1, 71), 70};
        case Op::div: return {"np.divide(" + v(0, 0) + ", " + v(1, 0) + ")"};
        case Op::pow: return {"np.power(" + v(0, 0) + ", " + v(1, 0) + ")"};
        case Op::call: {
            std::string args;
            for (std::size_t k = 0; k < n.args.size(); ++k) args += (k ? ", " : "") + v(k, 0);
            if (n.name == "sqrt" || n.name == "log10") return {"np." + n.name + "(" + args + ")"};
            return {n.name + "(" + args + ")"};
        }
        default: return {"float(" + condition(at).text + ")"};
        }
    }

    PyExpr condition(std::size_t at) const {
        using Op = core::Expr::Op;
        const auto& n = e_.nodes[at];
        const auto v = [&](std::size_t k) { return wrap(value(n.args[k]), 60); };
        const auto c = [&](std::size_t k, int prec) { return wrap(condition(n.args[k]), prec); };
        const auto compare = [&](const char* op) { return PyExpr{v(0) + " " + op + " " + v(1), 40}; };
        switch (n.op) {
        case Op::lt: return compare("<");
        case Op::le: return compare("<=");
        case Op::gt: return compare(">");
        case Op::ge: return compare(">=");
        case Op::eq: return compare("==");
        case Op::ne: return compare("!=");
        case Op::not_: return {"not " + c(0, 30), 30};
        case Op::and_: return {c(0, 20) + " and " + c(1, 20), 20};
        case Op::or_: return {c(0, 10) + " or " + c(1, 10), 10};
        case Op::name:
            if (name(n.name).second == "passed") return {name(n.name).first + "_ok"};
            [[fallthrough]];
        default: return {wrap(value(at), 60) + " != 0", 40};
        }
    }

private:
    std::pair<std::string, std::string> name(const std::string& full) const {
        const auto dot = full.find('.');
        const auto port = full.substr(0, dot);
        const std::size_t k = port == "a" ? 0 : port == "b" ? 1 : port == "c" ? 2 : 3;
        return {k < io_.in.size() ? io_.in[k] : std::string{"None"}, dot == std::string::npos ? std::string{} : full.substr(dot + 1)};
    }

    const core::Expr& e_;
    const NodeIo& io_;
    const Emitter& em_;
};

core::Result<std::string> assert_block(const NodeIo& io, Emitter& em) {
    const auto e = core::parse_expr(param_text(io.node.params, "expr"));
    if (!e) return std::unexpected{unsupported(io, "expr")};
    const auto v = AssertTranslator{*e, io, em}.value(e->root);
    const auto text = v.prec == 100 && v.text.starts_with("float(") ? v.text : "float(" + v.text + ")";
    return fmt::format("with np.errstate(all=\"ignore\"):\n    {0}_value = {1}\n{0} = {{\"value\": {0}_value}}\n{0}_ok = {0}_value != 0", io.out[0], text);
}

core::Result<std::string> lms(const NodeIo& io, Emitter& em) {
    em.function("lms");
    return fmt::format("{}, {} = lms({}, {}, {}, {}, {})", io.out[0], io.out[1], io.in[0], io.in[1], param_int(io.node.params, "taps"), em.number(param_real(io.node.params, "mu")),
                       param_text(io.node.params, "rule") == "lms" ? "False" : "True");
}

core::Result<std::string> wiener(const NodeIo& io, Emitter& em) {
    em.function("wiener");
    return fmt::format("{} = wiener({}, {}, {})", io.out[0], io.in[0], param_int(io.node.params, "size"), em.number(param_real(io.node.params, "noise")));
}

core::Result<std::string> spectral_subtract(const NodeIo& io, Emitter& em) {
    em.function("spectral_subtract");
    const auto lead = std::llround(param_real(io.node.params, "noise_s") * input_rate(io));
    const auto noise = io.in.size() > 1 && !io.in[1].empty() ? io.in[1] : std::string{"None"};
    return fmt::format("{} = spectral_subtract({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], noise, lead, param_int(io.node.params, "n"),
                       em.number(param_real(io.node.params, "alpha")), em.number(param_real(io.node.params, "beta")));
}

core::Result<std::string> deconvolve(const NodeIo& io, Emitter& em) {
    em.function("deconvolve");
    return fmt::format("{} = deconvolve({}, {}, {})", io.out[0], io.in[0], io.in[1], em.number(param_real(io.node.params, "snr_db")));
}

core::Result<std::string> lpc(const NodeIo& io, Emitter& em) {
    em.function("lpc");
    return fmt::format("{0}, {1} = lpc({2}, {3})\n{1}_ok = True", io.out[0], io.out[1], io.in[0], param_int(io.node.params, "order"));
}

core::Result<std::string> lpc_synth(const NodeIo& io, Emitter& em) {
    em.function("lpc_synth");
    return fmt::format("{} = lpc_synth({}, {})", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> spectrum_peaks(const NodeIo& io, Emitter& em) {
    em.function("spectrum_peaks");
    return fmt::format("{0} = spectrum_peaks({1}, {2}, {3}, {4}, {5})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], param_int(io.node.params, "count"),
                       param_int(io.node.params, "segment"), em.number(param_real(io.node.params, "prominence_db")));
}

core::Result<std::string> band_power(const NodeIo& io, Emitter& em) {
    em.function("band_power");
    return fmt::format("{0} = band_power({1}, {2}, {3}, {4})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], param_text(io.node.params, "bands") == "third" ? "True" : "False",
                       param_int(io.node.params, "segment"));
}

std::string real_param(const NodeIo& io, Emitter& em, const char* name) { return em.number(param_real(io.node.params, name)); }

core::Result<std::string> pitch(const NodeIo& io, Emitter& em) {
    em.function("pitch");
    return fmt::format("{0}, {1} = pitch({2}, {3}, {4}, {5}, {6}, {7}, {8})\n{0}_ok = True", io.out[0], io.out[1], io.in[0], io.in_fs[0], param_int(io.node.params, "frame"),
                       param_int(io.node.params, "hop"), real_param(io, em, "fmin"), real_param(io, em, "fmax"), real_param(io, em, "threshold"));
}

std::string sst_args(const NodeIo& io, Emitter& em) {
    return fmt::format("{}, {}, {}, {}, {}, {}", io.in[0], io.in_fs[0], real_param(io, em, "lo"), real_param(io, em, "hi"), param_int(io.node.params, "voices"), real_param(io, em, "mu"));
}

core::Result<std::string> sst_band(const NodeIo& io, Emitter& em) {
    em.function("sst_band");
    return fmt::format("{} = sst_band({})", io.out[0], sst_args(io, em));
}

core::Result<std::string> sst_ridge(const NodeIo& io, Emitter& em) {
    em.function("sst_ridge");
    return fmt::format("{0}, {1}, {2} = sst_ridge({3})\n{0}_ok = True", io.out[0], io.out[1], io.out[2], sst_args(io, em));
}

core::Result<std::string> mfcc(const NodeIo& io, Emitter& em) {
    em.function("mfcc");
    return fmt::format("{0} = mfcc({1}, {2}, {3}, {4}, {5}, {6})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], param_int(io.node.params, "n_mfcc"),
                       param_int(io.node.params, "n_mels"), param_int(io.node.params, "frame"), param_int(io.node.params, "hop"));
}

core::Result<std::string> spectral_features(const NodeIo& io, Emitter& em) {
    em.function("spectral_features");
    return fmt::format("{0} = spectral_features({1}, {2}, {3}, {4}, {5})\n{0}_ok = True", io.out[0], io.in[0], io.in_fs[0], param_int(io.node.params, "frame"),
                       param_int(io.node.params, "hop"), real_param(io, em, "rolloff_pct"));
}

core::Result<std::string> histogram(const NodeIo& io, Emitter& em) {
    const auto& text = param_text(io.node.params, "range");
    std::string limits = "None";
    if (text != "auto") {
        const auto v = pipeline::blocks::parse_numbers("histogram", "range", text);
        if (!v || v->size() != 2) return std::unexpected{unsupported(io, "range")};
        limits = "(" + em.number((*v)[0]) + ", " + em.number((*v)[1]) + ")";
    }
    em.function("histogram");
    return fmt::format("{0} = histogram({1}, {2}, {3})\n{0}_ok = True", io.out[0], io.in[0], param_int(io.node.params, "bins"), limits);
}

core::Result<std::string> delay_estimate(const NodeIo& io, Emitter& em) {
    em.function("delay_estimate");
    return fmt::format("{0} = delay_estimate({1}, {2}, {3})\n{0}_ok = True", io.out[0], io.in[0], io.in[1], io.in_fs[0]);
}

core::Result<std::string> correlation(const NodeIo& io, Emitter& em) {
    em.function("correlation");
    return fmt::format("{0} = correlation({1}, {2})\n{0}_ok = True", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> coeff_stats(const NodeIo& io, Emitter& em) {
    const auto* c = !io.in_values.empty() && io.in_values[0] ? std::get_if<pipeline::Coeffs>(io.in_values[0]) : nullptr;
    std::string labels, weights;
    if (c) {
        for (std::size_t b = 0; b <= c->levels(); ++b) labels += (b ? ", " : "") + py_str(pipeline::band_label(*c, b));
        if (c->transform == "swt")
            for (const double w : core::swt_energy_weights(c->levels(), c->norm)) weights += (weights.empty() ? "" : ", ") + em.number(w);
    }
    em.function("coeff_stats");
    return fmt::format("{0} = coeff_stats({1}, [{2}], [{3}], {4})\n{0}_ok = True", io.out[0], io.in[0], labels, weights, real_param(io, em, "tol"));
}

}  // namespace

void register_analysis_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"stats", stats}, {"loudness", loudness}, {"assert", assert_block}, {"wiener", wiener}, {"spectral_subtract", spectral_subtract}, {"deconvolve", deconvolve}, {"lpc", lpc}, {"lpc_synth", lpc_synth},
             {"spectrum_peaks", spectrum_peaks}, {"band_power", band_power}, {"pitch", pitch}, {"mfcc", mfcc}, {"spectral_features", spectral_features},
             {"histogram", histogram}, {"delay_estimate", delay_estimate}, {"correlation", correlation}, {"coeff_stats", coeff_stats}})
        out.push_back({id, 1e-12, fn});
    out.push_back({"sst_band", 1e-10, sst_band});
    out.push_back({"sst_ridge", 1e-10, sst_ridge});
    out.push_back({"lms", 1e-10, lms});
}

}  // namespace signals::python
