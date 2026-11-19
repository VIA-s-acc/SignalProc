#include "python/script.hpp"

#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

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
    const auto at = band < c.basis.size() || band == 0 ? std::to_string(band) : "-" + std::to_string(c.levels() + 1 - band);
    const auto item = var + "[" + at + "]";
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
        prepare();
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

    std::string rhs(const std::string& value) const {
        if (pipeline::variables_in(value).empty()) {
            if (core::eval_number(value))
                if (auto e = em_.py_expression(value)) return *e;
            return py_str(pipeline::unescape_dollars(value));
        }
        if (auto e = em_.py_expression(value)) return *e;
        return em_.text_expression(value);
    }

    static bool integer_literal(const std::string& v) {
        const auto digits = v.starts_with('-') ? v.substr(1) : v;
        return !digits.empty() && std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
    }

    void declare_variables() {
        const auto& lets = doc_.lets;
        if (lets.empty()) return;
        for (const auto& l : lets) em_.variable(l.name, integer_literal(l.value));
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
        for (const auto k : order) var_lines_.push_back(em_.py_var(lets[k].name) + " = " + rhs(lets[k].value));
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
            if (!n.instance.empty()) {
                vars_[i].assign(n.type->outputs.size(), std::string{});
                continue;
            }
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

    struct Block {
        std::string code, node, type;
        std::vector<std::string> plots;
        bool own = false;
    };

    Input named(const std::string& var, const Value* v) const {
        Input in{var, v ? em_.number(rate_of(*v)) : std::string{"fs"}, v};
        if (v && std::holds_alternative<core::AudioBuffer>(*v)) in.fs = var + "_fs";
        return in;
    }

    Input with_band(Input whole, const std::string& band) {
        const auto* c = whole.value ? std::get_if<pipeline::Coeffs>(whole.value) : nullptr;
        if (!c) return whole;
        Input in = whole;
        if (const auto i = pipeline::band_index(*c, band)) {
            in.expr = band_expr(*c, *i, whole.expr);
            const auto level = *i < c->basis.size() ? c->basis[*i].level : *i == 0 ? c->levels() : c->levels() + 1 - *i;
            if (c->transform == "swt") in.fs = whole.expr + "_meta[\"rate\"]";
            else if (*i == 0 && c->basis.empty()) in.fs = fmt::format("{0}_meta[\"rate\"] / 2 ** (len({0}) - 1)", whole.expr);
            else in.fs = fmt::format("{}_meta[\"rate\"] / {}", whole.expr, std::size_t{1} << level);
            if (auto b = pipeline::band_port(*c, band)) {
                keep_.push_back(std::move(*b));
                in.value = &keep_.back();
            }
        }
        return in;
    }

    Input input_of(const pipeline::Source& s) {
        if (const auto it = outer_.find(key_of(s)); it != outer_.end()) return it->second;
        if (const auto it = outer_.find(SourceKey{s.node, s.output, std::string{}}); it != outer_.end()) return s.band.empty() ? it->second : with_band(it->second, s.band);
        auto whole = named(vars_[s.node][s.output], value_of(s.node, s.output));
        return s.band.empty() ? whole : with_band(std::move(whole), s.band);
    }

    core::Result<std::string> node_code(std::size_t i, const std::vector<Input>& inputs, const std::vector<std::string>& outs) {
        const auto& n = graph_.nodes[i];
        const auto* form = find_py_form(n.type->id);
        std::vector<std::string> in, fs;
        std::vector<const Value*> in_values;
        for (const auto& input : inputs) {
            in.push_back(input.expr);
            fs.push_back(input.expr.empty() ? std::string{} : input.fs);
            in_values.push_back(input.value);
        }
        std::vector<const Value*> out_values;
        for (std::size_t p = 0; p < n.type->outputs.size(); ++p) out_values.push_back(value_of(i, p));
        const NodeIo io{n, in, outs, fs, in_values, out_values, graph_.base_dir};
        if (!form) return std::unexpected{unsupported(io, "")};
        auto code = n.bypass ? core::Result<std::string>{bypass_code(n, in, outs)} : form->emit(io, em_);
        if (!code) return std::unexpected{code.error()};
        for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
            if (n.type->outputs[p].type != pipeline::PortType::signal || defines(*code, outs[p] + "_fs")) continue;
            *code += "\n" + outs[p] + "_fs = " + output_rate(io, p, em_);
        }
        return code;
    }

    void add_plots(Block& block, const std::function<bool(const pipeline::Probe&)>& mine) {
        if (!em_.notebook()) return;
        for (const auto& p : graph_.probes) {
            if (!p.source.band.empty() || !mine(p)) continue;
            const auto in = input_of(p.source);
            if (in.expr.empty() || !in.value || !std::holds_alternative<core::AudioBuffer>(*in.value)) continue;
            em_.function("plot_signal");
            block.plots.push_back(fmt::format("plot_signal({}, {}, {})", in.expr, in.fs, py_str(p.name)));
        }
    }

    core::Result<void> emit_top_node(std::size_t i) {
        const auto& n = graph_.nodes[i];
        std::vector<Input> inputs;
        for (const auto& s : n.sources) inputs.push_back(s ? input_of(*s) : Input{});
        em_.set_node(n.id);
        auto code = node_code(i, inputs, vars_[i]);
        if (!code) return std::unexpected{code.error()};
        Block block{comment_for(n) + "\n" + *code, n.id, n.type->id, {}};
        add_plots(block, [&](const pipeline::Probe& p) { return p.source.node == i; });
        blocks_.push_back(std::move(block));
        return {};
    }

    core::Result<void> emit_nodes() {
        if (defs_.empty()) {
            for (const auto i : graph_.order)
                if (auto ok = emit_top_node(i); !ok) return ok;
            em_.set_node("");
            return {};
        }
        std::vector<std::string> items;
        std::map<std::string, std::size_t, std::less<>> item_index;
        std::vector<std::size_t> item_of(graph_.nodes.size(), 0);
        for (const auto i : graph_.order) {
            const auto& instance = graph_.nodes[i].instance;
            const auto name = instance.empty() ? "\x1f" + std::to_string(i) : instance.substr(0, instance.find('/'));
            const auto [it, fresh] = item_index.emplace(name, items.size());
            if (fresh) items.push_back(name);
            item_of[i] = it->second;
        }
        std::vector<std::set<std::size_t>> next(items.size());
        std::vector<std::size_t> pending(items.size(), 0);
        for (std::size_t i = 0; i < graph_.nodes.size(); ++i)
            for (const auto& s : graph_.nodes[i].sources)
                if (s && item_of[s->node] != item_of[i] && next[item_of[s->node]].insert(item_of[i]).second) ++pending[item_of[i]];
        std::set<std::size_t> ready;
        for (std::size_t k = 0; k < items.size(); ++k)
            if (pending[k] == 0) ready.insert(k);
        std::size_t done = 0;
        while (!ready.empty()) {
            const auto k = *ready.begin();
            ready.erase(ready.begin());
            ++done;
            const auto& name = items[k];
            auto ok = name.starts_with('\x1f') ? emit_top_node(std::stoul(name.substr(1))) : emit_top_call(*decl_in(doc_.nodes, name));
            if (!ok) return ok;
            for (const auto m : next[k])
                if (--pending[m] == 0) ready.insert(m);
        }
        em_.set_node("");
        for (std::size_t k = 0; k < items.size() && done < items.size(); ++k)
            if (pending[k] > 0 && !items[k].starts_with('\x1f'))
                return core::make_error(core::Errc::unsupported, fmt::format(fmt::runtime(tr("python.block_loop")), fmt::arg("call", items[k])));
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

    std::string bypass_code(const pipeline::GraphNode& n, const std::vector<std::string>& in, const std::vector<std::string>& outs) const {
        const auto from = in.empty() || in.front().empty() ? std::string{"None"} : in.front();
        std::string code;
        for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
            const auto& out = outs[p];
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
            const auto expr = input_of(p.source).expr;
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
                if (auto ok = add_value(what, input_of({i, p, {}}).expr, *v, "ref." + what, chain_[i]); !ok) return ok;
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
        for (const auto& f : block_functions_) out += "\n\n" + f;
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
        for (const auto& f : block_functions_) functions += (functions.empty() ? "" : "\n\n") + f;
        if (!functions.empty()) cells.push_back(cell("code", functions));
        std::string load = "HERE = Path.cwd()\nD = np.load(HERE / " + py_str(data) + ", allow_pickle=False)\n";
        for (const auto& line : em_.prelude()) load += line + "\n";
        cells.push_back(cell("code", load));
        for (const auto& b : blocks_) {
            cells.push_back(cell("markdown", b.own ? "**" + b.node + "** · `" + b.type + "` · " + tr("python.notebook.own_block")
                                                   : "**" + b.node + "** · `" + b.type + "` · " + tr("pipeline.block." + b.type) + "\n\n" + tr("pipeline.about." + b.type)));
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

    using SourceKey = std::tuple<std::size_t, std::size_t, std::string>;

    struct Resolved {
        std::optional<pipeline::Source> source;
        std::string through;
    };

    struct Frame {
        const pipeline::BlockDef* def = nullptr;
        std::string prefix;
        std::map<std::string, Input, std::less<>> args;
        std::map<std::string, std::vector<std::string>, std::less<>> vars;
        std::map<std::string, std::pair<const pipeline::BlockDef*, std::vector<Input>>, std::less<>> calls;
    };

    static SourceKey key_of(const pipeline::Source& s) { return {s.node, s.output, s.band}; }

    static std::string sanitized(std::string key) {
        for (auto& c : key)
            if (c == '/' || c == '.') c = '_';
        return key;
    }

    static std::string companion_suffix(const Value* v) {
        if (!v) return "_fs";
        if (std::holds_alternative<pipeline::Coeffs>(*v)) return "_meta";
        if (std::holds_alternative<pipeline::Report>(*v)) return "_ok";
        return "_fs";
    }

    static std::string companion(const Input& in) {
        if (in.expr.empty() || !in.value) return "None";
        if (std::holds_alternative<core::AudioBuffer>(*in.value)) return in.fs;
        if (std::holds_alternative<pipeline::Coeffs>(*in.value)) return in.expr + "_meta";
        if (std::holds_alternative<pipeline::Report>(*in.value)) return in.expr + "_ok";
        return "None";
    }

    static bool typed(const Value* v) {
        return v && (std::holds_alternative<core::AudioBuffer>(*v) || std::holds_alternative<pipeline::Coeffs>(*v) || std::holds_alternative<pipeline::Report>(*v));
    }

    static const pipeline::NodeDecl* decl_in(const std::vector<pipeline::NodeDecl>& nodes, std::string_view id) {
        const auto it = std::ranges::find(nodes, id, &pipeline::NodeDecl::id);
        return it == nodes.end() ? nullptr : &*it;
    }

    static std::optional<std::size_t> input_index(const pipeline::BlockType& t, std::string_view port) {
        const auto& ins = t.inputs;
        if (port.empty()) {
            if (ins.size() == 1) return 0;
            if (std::ranges::count_if(ins, [](const pipeline::PortSpec& p) { return !p.optional; }) == 1)
                return static_cast<std::size_t>(std::ranges::find_if(ins, [](const pipeline::PortSpec& p) { return !p.optional; }) - ins.begin());
            return std::nullopt;
        }
        for (std::size_t k = 0; k < ins.size(); ++k)
            if (ins[k].name == port) return k;
        return std::nullopt;
    }

    void prepare() {
        for (std::size_t i = 0; i < graph_.nodes.size(); ++i) index_.emplace(graph_.nodes[i].id, i);
        for (const auto* list : {&doc_.imported, &doc_.blocks})
            for (const auto& b : *list) defs_.insert_or_assign(b.name, &b);
        chain_.resize(graph_.nodes.size());
        for (const auto i : graph_.order) {
            const auto& n = graph_.nodes[i];
            const auto* form = find_py_form(n.type->id);
            Chain c{form ? form->tol : 1e-12, form && form->tol > 1e-12 ? n.id : std::string{}};
            for (const auto& s : n.sources)
                if (s && chain_[s->node].tol > c.tol) c = chain_[s->node];
            chain_[i] = c;
            if (n.type->id == "dwt" || n.type->id == "swt") pywt_sign_ = true;
        }
        if (defs_.empty()) return;
        map_instances("", doc_.nodes);
        std::set<std::string, std::less<>> used;
        for (const auto& [instance, def] : instance_def_) used.insert(def->name);
        trace_name_ = em_.name_for("trace");
        em_.name_for("at");
        for (const auto& name : used) fn_names_.emplace(name, em_.name_for(name));
        find_traced();
    }

    void map_instances(const std::string& prefix, const std::vector<pipeline::NodeDecl>& nodes) {
        for (const auto& n : nodes) {
            const auto it = defs_.find(n.type);
            if (it == defs_.end() || n.bypass) continue;
            instance_def_.emplace(prefix + n.id, it->second);
            map_instances(prefix + n.id + "/", it->second->nodes);
        }
    }

    std::string trace_key(std::size_t i, std::size_t o) const {
        const auto& n = graph_.nodes[i];
        return n.type->outputs.size() == 1 ? n.id : n.id + "." + n.type->outputs[o].name;
    }

    void find_traced() {
        std::set<SourceKey> covered;
        for (const auto& n : doc_.nodes) {
            const auto it = defs_.find(n.type);
            if (it == defs_.end() || n.bypass) continue;
            for (const auto& q : it->second->outs)
                if (const auto r = resolve_out(*it->second, n.id + "/", q.name); r.source) covered.insert(key_of(*r.source));
        }
        const auto need = [&](std::size_t i, std::size_t o, const std::string& band) {
            const auto& instance = graph_.nodes[i].instance;
            if (instance.empty() || covered.contains({i, o, band}) || covered.contains({i, o, std::string{}})) return;
            needed_.insert({i, o});
            traced_[instance_def_.at(instance)->name].insert(trace_key(i, o).substr(instance.size() + 1));
            for (auto at = instance.find('/');; at = instance.find('/', at + 1)) {
                trace_defs_.insert(instance_def_.at(instance.substr(0, at))->name);
                if (at == std::string::npos) break;
            }
        };
        for (const auto& p : graph_.probes) need(p.source.node, p.source.output, p.source.band);
        for (std::size_t i = 0; i < graph_.nodes.size(); ++i) {
            const auto& n = graph_.nodes[i];
            for (std::size_t o = 0; o < n.type->outputs.size(); ++o)
                if (n.type->outputs[o].type == pipeline::PortType::report) need(i, o, {});
            if (n.type->side_effect)
                for (const auto& s : n.sources)
                    if (s) need(s->node, s->output, s->band);
        }
    }

    pipeline::Source source_of(std::size_t i, std::string_view port) const {
        const auto& outs = graph_.nodes[i].type->outputs;
        if (port.empty()) return {i, 0, {}};
        for (std::size_t k = 0; k < outs.size(); ++k)
            if (outs[k].name == port) return {i, k, {}};
        const auto coeffs = std::ranges::find_if(outs, [](const pipeline::PortSpec& p) { return p.type == pipeline::PortType::coeffs; });
        return {i, coeffs == outs.end() ? 0 : static_cast<std::size_t>(coeffs - outs.begin()), std::string{port}};
    }

    Resolved resolve_ref(const pipeline::BlockDef& def, const std::string& prefix, const pipeline::PortRef& from, int depth) const {
        if (depth > 64) return {};
        if (std::ranges::any_of(def.ins, [&](const pipeline::PortDecl& p) { return p.name == from.node; })) return {std::nullopt, from.node};
        if (const auto* call = decl_in(def.nodes, from.node))
            if (const auto it = defs_.find(call->type); it != defs_.end()) {
                const auto& callee = *it->second;
                if (call->bypass) return callee.ins.empty() ? Resolved{} : resolve_into(def, prefix, call->id, callee.ins.front().name, depth + 1);
                auto r = resolve_out(callee, prefix + call->id + "/", from.port.empty() ? callee.outs.front().name : from.port, depth + 1);
                if (r.source || r.through.empty()) return r;
                return resolve_into(def, prefix, call->id, r.through, depth + 1);
            }
        const auto it = index_.find(prefix + from.node);
        if (it == index_.end()) return {};
        return {source_of(it->second, from.port), {}};
    }

    Resolved resolve_out(const pipeline::BlockDef& def, const std::string& prefix, std::string_view port, int depth = 0) const {
        for (const auto& e : def.edges)
            if (e.to.node == port) return resolve_ref(def, prefix, e.from, depth);
        return {};
    }

    Resolved resolve_into(const pipeline::BlockDef& def, const std::string& prefix, std::string_view call, std::string_view port, int depth) const {
        const auto& callee = *defs_.at(decl_in(def.nodes, call)->type);
        for (const auto& e : def.edges)
            if (e.to.node == call && (e.to.port == port || (e.to.port.empty() && callee.ins.size() == 1))) return resolve_ref(def, prefix, e.from, depth);
        return {};
    }

    Input local_input(const Frame& f, const pipeline::PortRef& ref) {
        if (f.def && ref.port.empty())
            if (const auto a = f.args.find(ref.node); a != f.args.end()) return a->second;
        if (const auto c = f.calls.find(ref.node); c != f.calls.end()) {
            const auto& [callee, outs] = c->second;
            std::size_t k = 0;
            if (!ref.port.empty()) {
                while (k < callee->outs.size() && callee->outs[k].name != ref.port) ++k;
                if (k == callee->outs.size()) return {};
            }
            return k < outs.size() ? outs[k] : Input{};
        }
        const auto& nodes = f.def ? f.def->nodes : doc_.nodes;
        const auto& edges = f.def ? f.def->edges : doc_.edges;
        if (const auto* d = decl_in(nodes, ref.node); d && d->bypass && defs_.contains(d->type)) {
            for (const auto& e : edges)
                if (e.to.node == d->id) return local_input(f, e.from);
            return {};
        }
        const auto it = index_.find(f.prefix + ref.node);
        if (it == index_.end()) return {};
        const auto s = source_of(it->second, ref.port);
        if (!f.def) return input_of(s);
        const auto v = f.vars.find(ref.node);
        if (v == f.vars.end() || s.output >= v->second.size()) return {};
        auto whole = named(v->second[s.output], value_of(s.node, s.output));
        return s.band.empty() ? whole : with_band(std::move(whole), s.band);
    }

    std::vector<Input> call_args(const Frame& f, const pipeline::NodeDecl& call, const pipeline::BlockDef& callee) {
        std::vector<Input> out;
        const auto& edges = f.def ? f.def->edges : doc_.edges;
        for (const auto& q : callee.ins) {
            Input in;
            for (const auto& e : edges)
                if (e.to.node == call.id && (e.to.port == q.name || (e.to.port.empty() && callee.ins.size() == 1))) {
                    in = local_input(f, e.from);
                    break;
                }
            out.push_back(std::move(in));
        }
        return out;
    }

    Input out_input(const pipeline::BlockDef& callee, const std::string& prefix, std::string_view port, const std::vector<Input>& args, const std::string& var) {
        const Value* v = nullptr;
        if (const auto r = resolve_out(callee, prefix, port); r.source) {
            auto whole = named(var, value_of(r.source->node, r.source->output));
            v = r.source->band.empty() ? whole.value : with_band(std::move(whole), r.source->band).value;
        } else if (!r.through.empty()) {
            for (std::size_t k = 0; k < callee.ins.size() && k < args.size(); ++k)
                if (callee.ins[k].name == r.through) v = args[k].value;
        }
        return named(var, v);
    }

    static std::string targets(const std::vector<Input>& outs) {
        std::string out;
        for (const auto& o : outs) out += (out.empty() ? "" : ", ") + o.expr + ", " + o.expr + companion_suffix(o.value);
        return out;
    }

    std::string call_text(const pipeline::BlockDef& callee, const std::string& function, const pipeline::NodeDecl& call, const std::vector<Input>& args,
                          const pipeline::BlockDef* caller) const {
        const bool inside = caller != nullptr;
        std::vector<std::string> a;
        for (const auto& in : args) {
            a.push_back(in.expr.empty() ? "None" : in.expr);
            a.push_back(companion(in));
        }
        for (const auto& key : callee.order)
            if (const auto it = call.params.find(key); it != call.params.end()) a.push_back(param_names_.at(callee.name).at(key) + "=" + rhs(it->second));
        if (trace_defs_.contains(callee.name) && (!inside || trace_defs_.contains(caller->name))) {
            a.push_back("trace=" + (inside ? std::string{"trace"} : trace_name_));
            a.push_back("at=" + (inside ? "at + " + py_str(call.id + "/") : py_str(call.id + "/")));
        }
        std::string out = function + "(";
        for (std::size_t k = 0; k < a.size(); ++k) out += (k ? ", " : "") + a[k];
        return out + ")";
    }

    const std::vector<std::string>& source_lines(const std::filesystem::path& origin) {
        const auto key = origin.empty() ? std::string{} : utf8(origin);
        auto it = files_.find(key);
        if (it == files_.end()) {
            std::string text = o_.source;
            if (!origin.empty()) {
                std::ifstream in{origin, std::ios::binary};
                text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
            }
            it = files_.emplace(key, lines_of(text)).first;
        }
        return it->second;
    }

    std::string line_comment(const std::filesystem::path& origin, const pipeline::NodeDecl& n) {
        const auto& lines = source_lines(origin);
        if (n.line > 0 && static_cast<std::size_t>(n.line) <= lines.size()) {
            const auto& line = lines[static_cast<std::size_t>(n.line) - 1];
            const auto start = line.find_first_not_of(' ');
            return "# " + py_comment(start == std::string::npos ? line : line.substr(start));
        }
        return "# " + py_comment(n.id + " = " + n.type + "(…)");
    }

    static std::vector<std::pair<std::string, std::string>> ordered(std::vector<std::pair<std::string, std::string>> items) {
        std::vector<std::pair<std::string, std::string>> out;
        std::set<std::string, std::less<>> pending;
        for (const auto& [name, value] : items) pending.insert(name);
        while (!items.empty()) {
            const auto ready = std::ranges::find_if(items, [&](const auto& item) {
                return std::ranges::none_of(pipeline::variables_in(item.second), [&](const std::string& v) { return v != item.first && pending.contains(v); });
            });
            const auto next = ready == items.end() ? items.begin() : ready;
            pending.erase(next->first);
            out.push_back(std::move(*next));
            items.erase(next);
        }
        return out;
    }

    std::string default_literal(const std::string& value) const {
        if (pipeline::variables_in(value).empty()) return rhs(value);
        return py_str(pipeline::unescape_dollars(value));
    }

    core::Result<void> emit_top_call(const pipeline::NodeDecl& call) {
        const auto& def = *defs_.at(call.type);
        const auto prefix = call.id + "/";
        const auto args = call_args(top_, call, def);
        const auto function = emit_function(def, prefix, args);
        if (!function) return std::unexpected{function.error()};
        std::string code;
        if (trace_defs_.contains(def.name) && !trace_declared_) {
            code += trace_name_ + " = {}\n";
            trace_declared_ = true;
        }
        std::vector<Input> outs;
        for (const auto& q : def.outs) outs.push_back(out_input(def, prefix, q.name, args, em_.name_for(call.id + "_" + q.name)));
        code += outs.empty() ? call_text(def, *function, call, args, nullptr) : targets(outs) + " = " + call_text(def, *function, call, args, nullptr);
        for (std::size_t k = 0; k < def.outs.size(); ++k)
            if (const auto r = resolve_out(def, prefix, def.outs[k].name); r.source) outer_.emplace(key_of(*r.source), outs[k]);
        top_.calls[call.id] = {&def, outs};
        for (const auto& [i, o] : needed_) {
            const auto& instance = graph_.nodes[i].instance;
            if (instance.substr(0, instance.find('/')) != call.id) continue;
            const auto key = trace_key(i, o);
            const auto name = em_.name_for(sanitized(key));
            vars_[i][o] = name;
            code += "\n" + name + " = " + trace_name_ + "[" + py_str(key) + "]";
            if (typed(value_of(i, o))) {
                const auto suffix = companion_suffix(value_of(i, o));
                code += "\n" + name + suffix + " = " + trace_name_ + "[" + py_str(key + suffix) + "]";
            }
        }
        Block block{line_comment({}, call) + "\n" + code, call.id, def.name, {}, true};
        add_plots(block, [&](const pipeline::Probe& p) {
            const auto& instance = graph_.nodes[p.source.node].instance;
            return instance.substr(0, instance.find('/')) == call.id;
        });
        blocks_.push_back(std::move(block));
        return {};
    }

    std::string variant_key(const pipeline::BlockDef& def, const std::string& prefix, const std::vector<Input>& args) const {
        std::string key = def.name + "|";
        for (const auto& a : args) {
            if (a.expr.empty() || !a.value) key += '-';
            else if (std::holds_alternative<core::AudioBuffer>(*a.value)) key += 's';
            else if (std::holds_alternative<pipeline::Coeffs>(*a.value)) key += 'c';
            else if (std::holds_alternative<pipeline::Report>(*a.value)) key += 'r';
            else key += 'o';
        }
        for (auto it = index_.lower_bound(prefix); it != index_.end() && it->first.starts_with(prefix); ++it) {
            const auto& n = graph_.nodes[it->second];
            for (const auto& spec : n.type->params)
                if (spec.kind == pipeline::ParamSpec::Kind::path || (n.type->id == "assert" && spec.name == "expr"))
                    if (const auto v = n.params.find(spec.name); v != n.params.end()) key += "|" + it->first.substr(prefix.size()) + "." + spec.name + "=" + v->second;
        }
        return key;
    }

    core::Result<std::string> emit_function(const pipeline::BlockDef& def, const std::string& prefix, const std::vector<Input>& args) {
        const auto key = variant_key(def, prefix, args);
        if (const auto it = variants_.find(key); it != variants_.end()) return it->second;
        auto name = fn_names_.at(def.name);
        if (!function_names_.insert(name).second) {
            const auto made = std::ranges::count_if(variants_, [&](const auto& v) { return v.first.starts_with(def.name + "|"); });
            name = em_.global_name(name + "_" + std::to_string(made + 1));
            function_names_.insert(name);
        }
        variants_.emplace(key, name);
        std::set<std::string, std::less<>> taken{"trace", "at"};
        for (const auto& [base, fn] : fn_names_) taken.insert(fn);
        taken.insert(function_names_.begin(), function_names_.end());
        em_.enter_scope(std::move(taken));
        auto text = function_text(def, name, prefix, args);
        em_.leave_scope();
        if (!text) return std::unexpected{text.error()};
        block_functions_.push_back(std::move(*text));
        return name;
    }

    core::Result<std::string> function_text(const pipeline::BlockDef& def, const std::string& function, const std::string& prefix, const std::vector<Input>& args) {
        Frame f{&def, prefix, {}, {}, {}};
        std::vector<std::string> params;
        for (std::size_t k = 0; k < def.ins.size(); ++k) {
            const auto name = em_.name_for(def.ins[k].name);
            const bool wired = k < args.size() && !args[k].expr.empty() && args[k].value;
            const auto* v = wired ? args[k].value : nullptr;
            f.args[def.ins[k].name] = wired ? named(name, v) : Input{};
            params.push_back(name);
            params.push_back(name + companion_suffix(v));
        }
        auto& names = param_names_[def.name];
        std::vector<std::pair<std::string, std::string>> derived;
        std::set<std::string, std::less<>> defaulted;
        for (const auto& key : def.order) {
            const auto& value = def.defaults.at(key);
            const bool computed = !pipeline::variables_in(value).empty();
            const auto var = em_.variable(key, !computed && integer_literal(value));
            names[key] = var;
            params.push_back(var + "=" + (computed ? std::string{"None"} : default_literal(value)));
            if (computed) {
                derived.emplace_back(key, value);
                defaulted.insert(key);
            }
        }
        for (const auto& l : def.lets) {
            em_.variable(l.name, integer_literal(l.value));
            derived.emplace_back(l.name, l.value);
        }
        std::string body;
        for (const auto& [name, value] : ordered(derived)) {
            const auto var = em_.py_var(name);
            body += "    " + var + " = " + rhs(value) + (defaulted.contains(name) ? " if " + var + " is None else " + var : std::string{}) + "\n";
        }
        const bool traced = trace_defs_.contains(def.name);
        if (traced) {
            params.emplace_back("trace=None");
            params.emplace_back("at=\"\"");
        }
        std::map<std::string, std::size_t, std::less<>> at;
        for (std::size_t k = 0; k < def.nodes.size(); ++k) at.emplace(def.nodes[k].id, k);
        std::vector<std::set<std::size_t>> next(def.nodes.size());
        std::vector<std::size_t> pending(def.nodes.size(), 0);
        for (const auto& e : def.edges) {
            const auto a = at.find(e.from.node), b = at.find(e.to.node);
            if (a == at.end() || b == at.end() || a->second == b->second) continue;
            if (next[a->second].insert(b->second).second) ++pending[b->second];
        }
        std::set<std::size_t> ready;
        for (std::size_t k = 0; k < def.nodes.size(); ++k)
            if (pending[k] == 0) ready.insert(k);
        const auto add = [&](const std::string& code) {
            for (const auto& line : lines_of(code)) body += (line.empty() ? std::string{} : "    " + line) + "\n";
        };
        std::size_t emitted = 0;
        while (!ready.empty()) {
            const auto k = *ready.begin();
            ready.erase(ready.begin());
            ++emitted;
            for (const auto m : next[k])
                if (--pending[m] == 0) ready.insert(m);
            const auto& n = def.nodes[k];
            if (const auto it = defs_.find(n.type); it != defs_.end()) {
                const auto& callee = *it->second;
                const auto inputs = call_args(f, n, callee);
                std::vector<Input> outs;
                std::string code;
                if (n.bypass) {
                    const auto in = inputs.empty() ? Input{} : inputs.front();
                    const auto var = em_.name_for(n.id + "_" + callee.outs.front().name);
                    outs.push_back(named(var, in.value));
                    code = var + " = " + (in.expr.empty() ? "None" : in.expr) + "\n" + var + companion_suffix(in.value) + " = " + companion(in);
                } else {
                    const auto called = emit_function(callee, prefix + n.id + "/", inputs);
                    if (!called) return std::unexpected{called.error()};
                    for (const auto& q : callee.outs) outs.push_back(out_input(callee, prefix + n.id + "/", q.name, inputs, em_.name_for(n.id + "_" + q.name)));
                    code = outs.empty() ? call_text(callee, *called, n, inputs, &def) : targets(outs) + " = " + call_text(callee, *called, n, inputs, &def);
                }
                add(line_comment(def.origin, n) + "\n" + code);
                f.calls[n.id] = {&callee, std::move(outs)};
                continue;
            }
            const auto found = index_.find(prefix + n.id);
            if (found == index_.end()) return core::make_error(core::Errc::unsupported, "missing node " + prefix + n.id);
            const auto i = found->second;
            const auto& g = graph_.nodes[i];
            auto& vars = f.vars[n.id];
            if (g.type->outputs.size() == 1) vars = {em_.name_for(n.id)};
            else
                for (const auto& p : g.type->outputs) vars.push_back(em_.name_for(n.id + "_" + p.name));
            std::vector<Input> inputs(g.type->inputs.size());
            for (const auto& e : def.edges)
                if (e.to.node == n.id)
                    if (const auto q = input_index(*g.type, e.to.port))
                        if (auto in = local_input(f, e.from); !in.expr.empty()) inputs[*q] = std::move(in);
            em_.set_node(function + "/" + n.id);
            auto code = node_code(i, inputs, vars);
            if (!code) return std::unexpected{code.error()};
            std::string traces;
            if (const auto t = traced_.find(def.name); t != traced_.end())
                for (std::size_t o = 0; o < vars.size(); ++o) {
                    const auto local = g.type->outputs.size() == 1 ? n.id : n.id + "." + g.type->outputs[o].name;
                    if (!t->second.contains(local)) continue;
                    traces += "\n    trace[at + " + py_str(local) + "] = " + vars[o];
                    if (typed(value_of(i, o))) {
                        const auto suffix = companion_suffix(value_of(i, o));
                        traces += "\n    trace[at + " + py_str(local + suffix) + "] = " + vars[o] + suffix;
                    }
                }
            if (!traces.empty()) *code += "\nif trace is not None:" + traces;
            add(line_comment(def.origin, n) + "\n" + *code);
        }
        if (emitted < def.nodes.size()) return core::make_error(core::Errc::unsupported, fmt::format(fmt::runtime(tr("python.block_loop")), fmt::arg("call", def.name)));
        std::string returns;
        for (const auto& q : def.outs) {
            Input r;
            for (const auto& e : def.edges)
                if (e.to.node == q.name) {
                    r = local_input(f, e.from);
                    break;
                }
            returns += (returns.empty() ? "" : ", ") + (r.expr.empty() ? std::string{"None"} : r.expr) + ", " + companion(r);
        }
        if (!returns.empty()) body += "    return " + returns + "\n";
        if (body.empty()) body = "    pass\n";
        std::string head;
        for (const auto& line : def.comments.above) head += (line.starts_with('#') ? py_comment(line) : "# " + py_comment(line)) + "\n";
        std::string list;
        for (const auto& param : params) list += (list.empty() ? "" : ", ") + param;
        return head + "def " + function + "(" + list + "):\n" + body;
    }

    const pipeline::PipelineDoc& doc_;
    const pipeline::Graph& graph_;
    const ScriptOptions& o_;
    pipeline::RunResult result_;
    Emitter em_;
    std::vector<std::vector<std::string>> vars_;
    std::vector<Chain> chain_;
    std::vector<Block> blocks_;
    std::vector<Row> rows_;
    std::deque<Value> keep_;
    bool pywt_sign_ = false;
    std::vector<std::string> var_lines_;
    std::string graph_line_, now_;
    std::map<std::string, std::size_t, std::less<>> index_;
    std::map<std::string, const pipeline::BlockDef*, std::less<>> defs_, instance_def_;
    std::map<std::string, std::string, std::less<>> fn_names_;
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> param_names_;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> traced_;
    std::set<std::string, std::less<>> trace_defs_, function_names_;
    std::map<std::string, std::string, std::less<>> variants_;
    std::set<std::pair<std::size_t, std::size_t>> needed_;
    std::map<SourceKey, Input> outer_;
    std::map<std::string, std::vector<std::string>, std::less<>> files_;
    std::vector<std::string> block_functions_;
    Frame top_;
    std::string trace_name_;
    bool trace_declared_ = false;
};

}  // namespace

core::Result<Script> build_script(const pipeline::PipelineDoc& doc, const pipeline::Graph& graph, const ScriptOptions& options, const core::CancelToken& cancel) {
    Builder b{doc, graph, options};
    return b.run(cancel);
}

}  // namespace signals::python
