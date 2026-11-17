#include "python/script.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "core/expr.hpp"
#include "pipeline/docs.hpp"
#include "pipeline/flatten.hpp"
#include "pipeline/sweep.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/npz_values.hpp"
#include "python/form.hpp"

#ifndef SIGNALS_BRIDGE_VERSIONS
#define SIGNALS_BRIDGE_VERSIONS ""
#endif

namespace signals::python {
namespace {

using pipeline::Value;

struct Chain {
    double tol = 1e-12;
    std::string origin;
};

enum class RowKind { number, decibel, text, file_name };

struct Row {
    std::string what, expr, ref;
    RowKind kind = RowKind::number;
    Chain chain;
};

std::string internal_probe(std::size_t node, std::size_t port) { return "\x1f" + std::to_string(node) + "." + std::to_string(port); }

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
    }
    return out;
}

double rate_of(const Value& v) {
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) return a->sample_rate;
    if (const auto* c = std::get_if<pipeline::Coeffs>(&v)) return c->rate;
    return 0;
}

bool detail_sign_differs(const pipeline::Coeffs& c) { return c.transform == "dwt" || c.transform == "swt"; }

std::string band_expr(const pipeline::Coeffs& c, std::size_t band, const std::string& var) {
    const auto item = var + "[" + std::to_string(band) + "]";
    return detail_sign_differs(c) && band > 0 ? "(-" + item + ")" : item;
}

std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return {u.begin(), u.end()};
}

class Builder {
public:
    Builder(const pipeline::PipelineDoc& doc, const pipeline::Graph& graph, const ScriptOptions& options) : doc_{doc}, graph_{graph}, o_{options} {}

    core::Result<Script> run(const core::CancelToken& cancel) {
        auto g = graph_;
        for (std::size_t i = 0; i < g.nodes.size(); ++i)
            for (std::size_t p = 0; p < g.nodes[i].type->outputs.size(); ++p)
                g.probes.push_back({internal_probe(i, p), pipeline::PortRef{g.nodes[i].id, g.nodes[i].type->outputs[p].name}, pipeline::Source{i, p, ""}});
        pipeline::ResultCache cache{0};
        auto result = pipeline::run_graph(g, cache, pipeline::RunEvents{o_.on_node}, cancel);
        if (!result) return std::unexpected{result.error()};
        result_ = std::move(*result);
        em_.set_out_dir(std::filesystem::absolute(o_.out).parent_path());
        em_.set_notebook(o_.kind == ScriptKind::notebook);
        declare_variables();
        name_nodes();
        if (auto ok = emit_nodes(); !ok) return std::unexpected{ok.error()};
        em_.function("verify");
        if (!em_.missing().empty()) return core::make_error(core::Errc::unsupported, "missing python function " + em_.missing().front());
        if (auto ok = collect_checks(); !ok) return std::unexpected{ok.error()};
        if (rows_.empty()) return core::make_error(core::Errc::invalid_argument, tr("python.nothing_to_check"));
        Script s;
        s.text = o_.kind == ScriptKind::notebook ? notebook() : text();
        s.data = em_.entries();
        for (const auto& r : rows_) s.checks.push_back({r.what, r.chain.origin, r.chain.tol});
        return s;
    }

private:
    const Value* value_of(std::size_t node, std::size_t port) const {
        const auto it = result_.probes.find(internal_probe(node, port));
        return it == result_.probes.end() ? nullptr : &it->second;
    }

    std::string let_rhs(const pipeline::LetDecl& l) const {
        if (pipeline::variables_in(l.value).empty()) {
            if (core::eval_number(l.value))
                if (auto e = em_.py_expression(l.value)) return *e;
            return py_str(pipeline::unescape_dollars(l.value));
        }
        if (auto e = em_.py_expression(l.value)) return *e;
        return em_.text_expression(l.value);
    }

    void declare_variables() {
        const auto& lets = doc_.lets;
        if (lets.empty()) return;
        const auto integer = [](const std::string& v) {
            const auto digits = v.starts_with('-') ? v.substr(1) : v;
            return !digits.empty() && std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
        };
        for (const auto& l : lets) em_.variable(l.name, integer(l.value));
        std::vector<bool> placed(lets.size(), false);
        std::vector<std::size_t> order;
        const auto ready = [&](std::size_t k) {
            for (const auto& name : pipeline::variables_in(lets[k].value))
                for (std::size_t j = 0; j < lets.size(); ++j)
                    if (lets[j].name == name && !placed[j]) return false;
            return true;
        };
        while (order.size() < lets.size()) {
            const auto before = order.size();
            for (std::size_t k = 0; k < lets.size(); ++k)
                if (!placed[k] && ready(k)) {
                    placed[k] = true;
                    order.push_back(k);
                    break;
                }
            if (order.size() == before) break;
        }
        for (const auto k : order) var_lines_.push_back(em_.py_var(lets[k].name) + " = " + let_rhs(lets[k]));
        const auto values = pipeline::let_values(lets);
        std::string graph, now;
        for (const auto& v : values) {
            graph += (graph.empty() ? "" : ", ") + py_str(v.name) + ": " + (v.number ? em_.number(*v.number) : py_str(v.expanded));
            now += (now.empty() ? "" : ", ") + py_str(v.name) + ": " + em_.py_var(v.name);
        }
        graph_line_ = "GRAPH = {" + graph + "}";
        now_ = "{" + now + "}";
        em_.function("graph_changed");
    }

    std::string variables_block() const {
        std::string out;
        for (const auto& line : var_lines_) out += line + "\n";
        return out + graph_line_ + "\n";
    }

    std::string changed_test() const { return "graph_changed(" + now_ + ", GRAPH)"; }

    void name_nodes() {
        vars_.resize(graph_.nodes.size());
        for (const auto i : graph_.order) {
            const auto& n = graph_.nodes[i];
            if (n.type->outputs.size() == 1) vars_[i] = {em_.name_for(n.id)};
            else
                for (const auto& p : n.type->outputs) vars_[i].push_back(em_.name_for(n.id + "_" + p.name));
        }
    }

    std::string comment_for(const pipeline::GraphNode& n) const {
        const auto lines = lines_of(o_.source);
        if (n.line > 0 && static_cast<std::size_t>(n.line) <= lines.size()) {
            auto line = lines[static_cast<std::size_t>(n.line) - 1];
            const auto start = line.find_first_not_of(' ');
            return "# " + py_comment(start == std::string::npos ? line : line.substr(start));
        }
        return "# " + py_comment(n.id + " = " + n.type->id + "(…)");
    }

    struct Input {
        std::string expr, fs;
        const Value* value = nullptr;
    };

    Input input_of(const pipeline::Source& s) {
        Input in;
        in.value = value_of(s.node, s.output);
        in.expr = vars_[s.node][s.output];
        const auto whole = in.expr;
        in.fs = in.value ? em_.number(rate_of(*in.value)) : std::string{"fs"};
        if (in.value && std::holds_alternative<core::AudioBuffer>(*in.value)) in.fs = whole + "_fs";
        if (!s.band.empty() && in.value) {
            const auto& c = std::get<pipeline::Coeffs>(*in.value);
            if (const auto i = pipeline::band_index(c, s.band)) {
                in.expr = band_expr(c, *i, in.expr);
                const auto level = *i < c.basis.size() ? c.basis[*i].level : *i == 0 ? c.levels() : c.levels() + 1 - *i;
                in.fs = c.transform == "swt" ? whole + "_meta[\"rate\"]" : fmt::format("{}_meta[\"rate\"] / {}", whole, std::size_t{1} << level);
                if (auto band = pipeline::band_port(c, s.band)) {
                    keep_.push_back(std::move(*band));
                    in.value = &keep_.back();
                }
            }
        }
        return in;
    }

    core::Result<void> emit_nodes() {
        chain_.resize(graph_.nodes.size());
        for (const auto i : graph_.order) {
            const auto& n = graph_.nodes[i];
            const auto* form = find_py_form(n.type->id);
            std::vector<std::string> in, fs;
            std::vector<const Value*> in_values;
            Chain c{form ? form->tol : 1e-12, form && form->tol > 1e-12 ? n.id : std::string{}};
            for (const auto& s : n.sources) {
                if (!s) {
                    in.emplace_back();
                    fs.emplace_back();
                    in_values.push_back(nullptr);
                    continue;
                }
                const auto input = input_of(*s);
                in.push_back(input.expr);
                fs.push_back(input.fs);
                in_values.push_back(input.value);
                if (chain_[s->node].tol > c.tol) c = chain_[s->node];
            }
            chain_[i] = c;
            std::vector<const Value*> outs;
            for (std::size_t p = 0; p < n.type->outputs.size(); ++p) outs.push_back(value_of(i, p));
            const NodeIo io{n, in, vars_[i], fs, in_values, outs, graph_.base_dir};
            if (!form) return std::unexpected{unsupported(io, "")};
            em_.set_node(n.id);
            auto code = n.bypass ? core::Result<std::string>{bypass_code(n, in)} : form->emit(io, em_);
            if (!code) return std::unexpected{code.error()};
            for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
                if (n.type->outputs[p].type != pipeline::PortType::signal || defines(*code, vars_[i][p] + "_fs")) continue;
                *code += "\n" + vars_[i][p] + "_fs = " + output_rate(io, p, em_);
            }
            if (n.type->id == "dwt" || n.type->id == "swt") pywt_sign_ = true;
            Block block{comment_for(n) + "\n" + *code, n.id, n.type->id, {}};
            if (em_.notebook())
                for (const auto& p : graph_.probes) {
                    if (p.source.node != i || !p.source.band.empty()) continue;
                    const auto* a = value_of(i, p.source.output) ? std::get_if<core::AudioBuffer>(value_of(i, p.source.output)) : nullptr;
                    if (!a) continue;
                    em_.function("plot_signal");
                    block.plots.push_back(fmt::format("plot_signal({0}, {0}_fs, {1})", vars_[i][p.source.output], py_str(p.name)));
                }
            blocks_.push_back(std::move(block));
        }
        em_.set_node("");
        return {};
    }

    static bool defines(const std::string& code, const std::string& name) {
        for (const auto& line : lines_of(code)) {
            const auto eq = line.find(" = ");
            if (eq == std::string::npos || line.starts_with(" ") || line.starts_with("#")) continue;
            std::string target = line.substr(0, eq) + ",";
            for (std::size_t at = 0; at < target.size();) {
                const auto comma = target.find(',', at);
                auto item = target.substr(at, comma - at);
                while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                while (!item.empty() && item.back() == ' ') item.pop_back();
                if (item == name) return true;
                at = comma + 1;
            }
        }
        return false;
    }

    std::string output_rate(const NodeIo& io, std::size_t port, Emitter& em) const {
        const auto* out = io.out_values[port] ? std::get_if<core::AudioBuffer>(io.out_values[port]) : nullptr;
        const double rate = out ? out->sample_rate : 0.0;
        for (std::size_t k = 0; k < io.in_values.size(); ++k)
            if (io.in_values[k] && std::holds_alternative<core::AudioBuffer>(*io.in_values[k]) && std::get<core::AudioBuffer>(*io.in_values[k]).sample_rate == rate) return io.in_fs[k];
        const auto& params = io.node.type->params;
        if (std::ranges::any_of(params, [](const pipeline::ParamSpec& s) { return s.name == "rate"; }))
            if (const auto v = pipeline::numeric(io.node.params.count("rate") ? io.node.params.at("rate") : std::string{}); v && *v == rate) return em.expr(io, "rate");
        return em.number(rate);
    }

    std::string bypass_code(const pipeline::GraphNode& n, const std::vector<std::string>& in) const {
        const auto from = in.empty() || in.front().empty() ? std::string{"None"} : in.front();
        std::string code;
        for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
            const auto& out = vars_[&n - graph_.nodes.data()][p];
            code += (code.empty() ? "" : "\n") + out + " = " + from;
            if (n.type->outputs[p].type == pipeline::PortType::coeffs) code += "\n" + out + "_meta = " + (from == "None" ? std::string{"None"} : from + "_meta");
            if (n.type->outputs[p].type == pipeline::PortType::report) code += "\n" + out + "_ok = " + (from == "None" ? std::string{"False"} : from + "_ok");
        }
        return code;
    }

    core::Result<void> add_value(const std::string& what, const std::string& expr, const Value& v, const std::string& prefix, const Chain& chain) {
        auto entries = pipeline::value_entries(v);
        if (!entries) return std::unexpected{entries.error()};
        for (auto& [key, array] : pipeline::prefixed(prefix, std::move(*entries))) em_.put(key, std::move(array));
        if (std::holds_alternative<core::AudioBuffer>(v)) {
            rows_.push_back({what, expr, prefix + ".x", RowKind::number, chain});
        } else if (const auto* c = std::get_if<pipeline::Coeffs>(&v)) {
            const auto keys = pipeline::band_keys(*c);
            for (std::size_t k = 0; k < keys.size(); ++k) {
                const auto item = expr + "[" + std::to_string(k) + "]";
                if (c->transform == "dtcwt" && k > 0) {
                    rows_.push_back({what + " · re_" + keys[k], item + "[..., 0::2]", prefix + ".re_" + keys[k], RowKind::number, chain});
                    rows_.push_back({what + " · im_" + keys[k], item + "[..., 1::2]", prefix + ".im_" + keys[k], RowKind::number, chain});
                } else {
                    rows_.push_back({what + " · " + keys[k], item, prefix + "." + keys[k], RowKind::number, chain});
                }
            }
        } else if (const auto* r = std::get_if<pipeline::Report>(&v)) {
            for (const auto& [field, value] : r->fields) {
                const auto item = expr + "[" + py_str(field) + "]";
                const bool text = std::holds_alternative<std::string>(value);
                const auto kind = text ? (field == "path" ? RowKind::file_name : RowKind::text) : field.ends_with("_db") ? RowKind::decibel : RowKind::number;
                rows_.push_back({what + " · " + field, item, prefix + "." + field, kind, chain});
            }
            rows_.push_back({what + " · passed", "float(" + expr + "_ok)", prefix + ".passed", RowKind::number, chain});
        }
        return {};
    }

    void loosen_text_size(std::size_t from, std::size_t node) {
        const auto& n = graph_.nodes[node];
        if (n.type->id != "export" && n.type->id != "export_report") return;
        const auto format = pipeline::param_text(n.params, "format");
        if (format == "npy" || format == "npz") return;
        for (auto k = from; k < rows_.size(); ++k)
            if (rows_[k].what.ends_with(" · bytes")) rows_[k].chain = {1e-2, n.id};
    }

    core::Result<void> collect_checks() {
        for (const auto& p : graph_.probes) {
            const auto it = result_.probes.find(p.name);
            if (it == result_.probes.end()) continue;
            auto expr = vars_[p.source.node][p.source.output];
            if (!p.source.band.empty())
                if (const auto* v = value_of(p.source.node, p.source.output))
                    if (const auto i = pipeline::band_index(std::get<pipeline::Coeffs>(*v), p.source.band)) expr = band_expr(std::get<pipeline::Coeffs>(*v), *i, expr);
            const auto from = rows_.size();
            if (auto ok = add_value(p.name, expr, it->second, "ref." + p.name, chain_[p.source.node]); !ok) return ok;
            loosen_text_size(from, p.source.node);
        }
        for (const auto i : graph_.order) {
            const auto& n = graph_.nodes[i];
            for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
                const auto* v = value_of(i, p);
                if (!v || !std::holds_alternative<pipeline::Report>(*v)) continue;
                const auto what = n.type->outputs.size() == 1 ? n.id : n.id + "." + n.type->outputs[p].name;
                const auto from = rows_.size();
                if (auto ok = add_value(what, vars_[i][p], *v, "ref." + what, chain_[i]); !ok) return ok;
                loosen_text_size(from, i);
            }
            if (!n.type->side_effect) continue;
            for (std::size_t k = 0; k < n.sources.size(); ++k) {
                if (!n.sources[k]) continue;
                const auto input = input_of(*n.sources[k]);
                if (!input.value || std::holds_alternative<pipeline::Report>(*input.value)) continue;
                const auto what = n.id + " ← " + n.type->inputs[k].name;
                if (auto ok = add_value(what, input.expr, *input.value, "ref." + n.id + ".in" + std::to_string(k), chain_[n.sources[k]->node]); !ok) return ok;
            }
        }
        return {};
    }

    std::string check_line(const Row& r) const {
        const auto ref = "D[" + py_str(r.ref) + "]";
        switch (r.kind) {
        case RowKind::text: return "verify_text(" + py_str(r.what) + ", " + r.expr + ", " + ref + ")";
        case RowKind::file_name: return "verify_name(" + py_str(r.what) + ", " + r.expr + ", " + ref + ")";
        case RowKind::number:
        case RowKind::decibel: break;
        }
        return std::string{r.kind == RowKind::decibel ? "verify_db(" : "verify("} + py_str(r.what) + ", " + r.expr + ", " + ref + ", " + fmt::format("{:.0e}", r.chain.tol) + ", " + py_str(r.chain.origin) + ")";
    }

    std::string text() const {
        const auto data_name = o_.out.stem().u8string() + u8".npz";
        const std::string data{data_name.begin(), data_name.end()};
        const auto script = utf8(o_.out.filename());
        std::string out = "# -*- coding: utf-8 -*-\n";
        out += "# " + fmt::format(fmt::runtime(tr("python.header.version")), fmt::arg("version", core::version()), fmt::arg("versions", SIGNALS_BRIDGE_VERSIONS)) + "\n";
        out += "# " + py_comment(fmt::format(fmt::runtime(tr("python.header.title")), fmt::arg("graph", doc_.name.empty() ? std::string{"—"} : doc_.name), fmt::arg("file", utf8(o_.sig.filename())))) + "\n";
        out += "# " + py_comment(fmt::format(fmt::runtime(tr("python.header.run")), fmt::arg("script", script), fmt::arg("data", data))) + "\n";
        if (pywt_sign_) out += "# " + tr("python.header.sign") + "\n";
        out += "\nimport sys\nfrom pathlib import Path\n\nimport numpy as np\n";
        for (const auto& line : em_.imports()) out += line + "\n";
        if (!var_lines_.empty()) out += "\n# " + tr("python.header.vars") + "\n" + variables_block();
        const auto functions = em_.functions();
        if (!functions.empty()) out += "\n";
        for (const auto& f : functions) out += "\n" + f + (f.ends_with('\n') ? "" : "\n");
        out += "\n\nHERE = Path(__file__).resolve().parent\n";
        out += "D = np.load(HERE / " + py_str(data) + ", allow_pickle=False)\n";
        for (const auto& line : em_.prelude()) out += line + "\n";
        for (const auto& b : blocks_) out += "\n" + b.code + (b.code.ends_with('\n') ? "" : "\n");
        out += "\n# " + tr("python.section.check") + "\n";
        if (!var_lines_.empty()) out += "if " + changed_test() + ":\n    print(" + py_str(tr("python.live.changed")) + ")\n    sys.exit(0)\n";
        out += check_rows();
        out += "if not report(" + report_args() + "):\n    sys.exit(1)\n";
        return out;
    }

    static nlohmann::json cell(const char* kind, std::string text) {
        while (text.ends_with('\n')) text.pop_back();
        auto source = nlohmann::json::array();
        for (std::size_t at = 0; at < text.size();) {
            const auto end = text.find('\n', at);
            source.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end + 1 - at));
            at = end == std::string::npos ? text.size() : end + 1;
        }
        nlohmann::json c{{"cell_type", kind}, {"metadata", nlohmann::json::object()}, {"source", std::move(source)}};
        if (std::string_view{kind} == "code") c["execution_count"] = nullptr, c["outputs"] = nlohmann::json::array();
        return c;
    }

    std::string check_rows() const {
        std::string out;
        for (const auto& r : rows_) out += check_line(r) + "\n";
        return out;
    }

    std::string report_args() const {
        return "[" + py_str(tr("python.check.what")) + ", " + py_str(tr("python.check.error")) + ", " + py_str(tr("python.check.tol")) + ", " + py_str(tr("python.check.origin")) + "]";
    }

    std::string notebook() const {
        const auto data_name = o_.out.stem().u8string() + u8".npz";
        const std::string data{data_name.begin(), data_name.end()};
        auto cells = nlohmann::json::array();
        std::string head = "# " + fmt::format(fmt::runtime(tr("python.notebook.title")), fmt::arg("graph", doc_.name.empty() ? std::string{"—"} : doc_.name), fmt::arg("file", utf8(o_.sig.filename())));
        head += "\n\n" + fmt::format(fmt::runtime(tr("python.header.version")), fmt::arg("version", core::version()), fmt::arg("versions", SIGNALS_NOTEBOOK_VERSIONS));
        head += "\n\n" + fmt::format(fmt::runtime(tr("python.notebook.run")), fmt::arg("data", data));
        if (pywt_sign_) head += "\n\n" + tr("python.header.sign");
        cells.push_back(cell("markdown", head));
        std::string imports = "from pathlib import Path\n\nimport numpy as np\n";
        for (const auto& line : em_.imports()) imports += line + "\n";
        cells.push_back(cell("code", imports));
        if (!var_lines_.empty()) {
            cells.push_back(cell("markdown", "### " + tr("python.notebook.vars") + "\n\n" + tr("python.notebook.vars.hint")));
            cells.push_back(cell("code", variables_block()));
        }
        std::string functions;
        for (const auto& f : em_.functions()) functions += (functions.empty() ? "" : "\n\n") + f;
        if (!functions.empty()) cells.push_back(cell("code", functions));
        std::string load = "HERE = Path.cwd()\nD = np.load(HERE / " + py_str(data) + ", allow_pickle=False)\n";
        for (const auto& line : em_.prelude()) load += line + "\n";
        cells.push_back(cell("code", load));
        for (const auto& b : blocks_) {
            cells.push_back(cell("markdown", "**" + b.node + "** · `" + b.type + "` · " + tr("pipeline.block." + b.type) + "\n\n" + tr("pipeline.about." + b.type)));
            cells.push_back(cell("code", b.code));
            for (const auto& plot : b.plots) cells.push_back(cell("code", plot));
        }
        cells.push_back(cell("markdown", "## " + tr("python.section.check")));
        const auto check = check_rows() + "assert report(" + report_args() + "), " + py_str(tr("python.notebook.failed"));
        if (var_lines_.empty()) cells.push_back(cell("code", check));
        else {
            std::string body = "if " + changed_test() + ":\n    print(" + py_str(tr("python.live.changed")) + ")\nelse:\n";
            for (const auto& line : lines_of(check)) body += "    " + line + "\n";
            cells.push_back(cell("code", body));
        }
        const nlohmann::json nb{{"cells", std::move(cells)},
                                {"metadata", {{"kernelspec", {{"display_name", "Python 3"}, {"language", "python"}, {"name", "python3"}}}, {"language_info", {{"name", "python"}}}}},
                                {"nbformat", 4},
                                {"nbformat_minor", 4}};
        return nb.dump(1) + "\n";
    }

    const pipeline::PipelineDoc& doc_;
    const pipeline::Graph& graph_;
    const ScriptOptions& o_;
    pipeline::RunResult result_;
    Emitter em_;
    std::vector<std::vector<std::string>> vars_;
    std::vector<Chain> chain_;
    struct Block {
        std::string code, node, type;
        std::vector<std::string> plots;
    };
    std::vector<Block> blocks_;
    std::vector<Row> rows_;
    std::deque<Value> keep_;
    bool pywt_sign_ = false;
    std::vector<std::string> var_lines_;
    std::string graph_line_, now_;
};

}  // namespace

core::Result<Script> build_script(const pipeline::PipelineDoc& doc, const pipeline::Graph& graph, const ScriptOptions& options, const core::CancelToken& cancel) {
    Builder b{doc, graph, options};
    return b.run(cancel);
}

}  // namespace signals::python
