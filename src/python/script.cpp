#include "python/script.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
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
        auto result = pipeline::run_graph(g, cache, {}, cancel);
        if (!result) return std::unexpected{result.error()};
        result_ = std::move(*result);
        em_.set_out_dir(std::filesystem::absolute(o_.out).parent_path());
        em_.set_notebook(o_.kind == ScriptKind::notebook);
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
        if (!s.band.empty() && in.value) {
            const auto& c = std::get<pipeline::Coeffs>(*in.value);
            if (const auto i = pipeline::band_index(c, s.band)) {
                in.expr = band_expr(c, *i, in.expr);
                if (auto band = pipeline::band_port(c, s.band)) {
                    keep_.push_back(std::move(*band));
                    in.value = &keep_.back();
                }
            }
        }
        in.fs = in.value ? em_.number(rate_of(*in.value)) : std::string{"fs"};
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
            if (n.type->id == "dwt" || n.type->id == "swt") pywt_sign_ = true;
            Block block{comment_for(n) + "\n" + *code, n.id, n.type->id, {}};
            if (em_.notebook())
                for (const auto& p : graph_.probes) {
                    if (p.source.node != i || !p.source.band.empty()) continue;
                    const auto* a = value_of(i, p.source.output) ? std::get_if<core::AudioBuffer>(value_of(i, p.source.output)) : nullptr;
                    if (!a) continue;
                    em_.function("plot_signal");
                    block.plots.push_back(fmt::format("plot_signal({}, {}, {})", vars_[i][p.source.output], em_.number(a->sample_rate), py_str(p.name)));
                }
            blocks_.push_back(std::move(block));
        }
        em_.set_node("");
        return {};
    }

    std::string bypass_code(const pipeline::GraphNode& n, const std::vector<std::string>& in) const {
        const auto from = in.empty() || in.front().empty() ? std::string{"None"} : in.front();
        std::string code;
        for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
            const auto& out = vars_[&n - graph_.nodes.data()][p];
            code += (code.empty() ? "" : "\n") + out + " = " + from;
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

    core::Result<void> collect_checks() {
        for (const auto& p : graph_.probes) {
            const auto it = result_.probes.find(p.name);
            if (it == result_.probes.end()) continue;
            auto expr = vars_[p.source.node][p.source.output];
            if (!p.source.band.empty())
                if (const auto* v = value_of(p.source.node, p.source.output))
                    if (const auto i = pipeline::band_index(std::get<pipeline::Coeffs>(*v), p.source.band)) expr = band_expr(std::get<pipeline::Coeffs>(*v), *i, expr);
            if (auto ok = add_value(p.name, expr, it->second, "ref." + p.name, chain_[p.source.node]); !ok) return ok;
        }
        for (const auto i : graph_.order) {
            const auto& n = graph_.nodes[i];
            for (std::size_t p = 0; p < n.type->outputs.size(); ++p) {
                const auto* v = value_of(i, p);
                if (!v || !std::holds_alternative<pipeline::Report>(*v)) continue;
                const auto what = n.type->outputs.size() == 1 ? n.id : n.id + "." + n.type->outputs[p].name;
                if (auto ok = add_value(what, vars_[i][p], *v, "ref." + what, chain_[i]); !ok) return ok;
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
        const auto functions = em_.functions();
        if (!functions.empty()) out += "\n";
        for (const auto& f : functions) out += "\n" + f + (f.ends_with('\n') ? "" : "\n");
        out += "\n\nHERE = Path(__file__).resolve().parent\n";
        out += "D = np.load(HERE / " + py_str(data) + ", allow_pickle=False)\n";
        for (const auto& line : em_.prelude()) out += line + "\n";
        for (const auto& b : blocks_) out += "\n" + b.code + (b.code.ends_with('\n') ? "" : "\n");
        out += "\n# " + tr("python.section.check") + "\n" + check_rows();
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
        cells.push_back(cell("code", check_rows() + "assert report(" + report_args() + "), " + py_str(tr("python.notebook.failed"))));
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
};

}  // namespace

core::Result<Script> build_script(const pipeline::PipelineDoc& doc, const pipeline::Graph& graph, const ScriptOptions& options, const core::CancelToken& cancel) {
    Builder b{doc, graph, options};
    return b.run(cancel);
}

}  // namespace signals::python
