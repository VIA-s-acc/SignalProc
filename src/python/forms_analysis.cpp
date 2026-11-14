#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/expr.hpp"
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
    if (!window || !hop) return std::unexpected{unsupported(io, "window")};
    em.function("loudness");
    return fmt::format("{0} = loudness({1}, {2}, {3})\n{0}_ok = True", io.out[0], io.in[0], *window, *hop);
}

std::string translate(const core::Expr& e, std::size_t at, const NodeIo& io, Emitter& em) {
    using Op = core::Expr::Op;
    const auto& n = e.nodes[at];
    const auto arg = [&](std::size_t k) { return "(" + translate(e, n.args[k], io, em) + ")"; };
    switch (n.op) {
    case Op::number: return em.number(n.value);
    case Op::name: {
        const auto dot = n.name.find('.');
        const auto port = n.name.substr(0, dot);
        const auto field = dot == std::string::npos ? std::string{} : n.name.substr(dot + 1);
        const std::size_t k = port == "a" ? 0 : port == "b" ? 1 : port == "c" ? 2 : 3;
        const auto var = k < io.in.size() ? io.in[k] : std::string{"None"};
        if (field == "passed") return "float(" + var + "_ok)";
        return var + "[" + py_str(field) + "]";
    }
    case Op::neg: return "-" + arg(0);
    case Op::plus: return arg(0);
    case Op::not_: return "float(" + arg(0) + " == 0)";
    case Op::add: return arg(0) + " + " + arg(1);
    case Op::sub: return arg(0) + " - " + arg(1);
    case Op::mul: return arg(0) + " * " + arg(1);
    case Op::div: return arg(0) + " / " + arg(1);
    case Op::pow: return arg(0) + " ** " + arg(1);
    case Op::lt: return "float(" + arg(0) + " < " + arg(1) + ")";
    case Op::le: return "float(" + arg(0) + " <= " + arg(1) + ")";
    case Op::gt: return "float(" + arg(0) + " > " + arg(1) + ")";
    case Op::ge: return "float(" + arg(0) + " >= " + arg(1) + ")";
    case Op::eq: return "float(" + arg(0) + " == " + arg(1) + ")";
    case Op::ne: return "float(" + arg(0) + " != " + arg(1) + ")";
    case Op::and_: return "float(" + arg(0) + " != 0 and " + arg(1) + " != 0)";
    case Op::or_: return "float(" + arg(0) + " != 0 or " + arg(1) + " != 0)";
    case Op::call: {
        std::string args;
        for (std::size_t k = 0; k < n.args.size(); ++k) args += (k ? ", " : "") + translate(e, n.args[k], io, em);
        if (n.name == "abs") return "abs(" + args + ")";
        if (n.name == "sqrt") return "math.sqrt(" + args + ")";
        if (n.name == "log10") return "math.log10(" + args + ")";
        return (n.name == "min" ? "min(" : "max(") + args + ")";
    }
    }
    return "0.0";
}

core::Result<std::string> assert_block(const NodeIo& io, Emitter& em) {
    const auto e = core::parse_expr(param_text(io.node.params, "expr"));
    if (!e) return std::unexpected{unsupported(io, "expr")};
    em.import_line("import math");
    return fmt::format("{0}_value = float({1})\n{0} = {{\"value\": {0}_value}}\n{0}_ok = {0}_value != 0", io.out[0], translate(*e, e->root, io, em));
}

}  // namespace

void register_analysis_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"stats", stats}, {"loudness", loudness}, {"assert", assert_block}})
        out.push_back({id, 1e-12, fn});
}

}  // namespace signals::python
