#include "pipeline/graph.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

struct Failure {
    SigError error;
};

[[noreturn]] void fail(int line, int column, std::string message) { throw Failure{{line, column, std::move(message)}}; }

template <typename... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

std::string type_name(PortType t) { return tr("pipeline.type." + std::string{to_string(t)}); }

std::string ref_text(const PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; }

bool digits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

bool band_name(std::string_view s) { return s == "a" || (s.size() > 1 && (s[0] == 'a' || s[0] == 'd') && digits(s.substr(1)) && s.substr(1) != "0"); }

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

bool bypassable(const BlockType& t) {
    if (t.inputs.empty()) return false;
    return std::all_of(t.outputs.begin(), t.outputs.end(), [&](const PortSpec& o) { return o.type == t.inputs.front().type; });
}

struct Wire {
    std::size_t from = 0, to = 0;
    int line = 0;
};

class Builder {
public:
    Builder(const PipelineDoc& doc, std::filesystem::path base_dir, BuildOptions options) : doc_{doc}, options_{options} {
        graph_.base_dir = std::move(base_dir);
    }

    Graph run() {
        add_nodes();
        add_wires();
        if (!options_.partial) check_inputs();
        sort();
        check_bands();
        add_probes();
        return std::move(graph_);
    }

private:
    void add_nodes() {
        std::vector<std::string> type_ids;
        for (const auto& t : block_types()) type_ids.push_back(t.id);
        for (const auto& decl : doc_.nodes) {
            const auto* type = find_block_type(decl.type);
            if (!type) {
                const auto hint = nearest_name(decl.type, type_ids);
                fail(decl.line, 0, hint ? say("pipeline.graph.unknown_block_hint", fmt::arg("block", decl.type), fmt::arg("hint", *hint))
                                        : say("pipeline.graph.unknown_block", fmt::arg("block", decl.type)));
            }
            std::vector<std::string> names;
            for (const auto& p : type->params) names.push_back(p.name);
            for (const auto& [key, value] : decl.params) {
                if (std::find(names.begin(), names.end(), key) != names.end()) continue;
                const auto hint = nearest_name(key, names);
                fail(decl.line, 0,
                     decl.id + ": " +
                         (hint ? say("pipeline.error.unknown_param_hint", fmt::arg("block", type->id), fmt::arg("param", key), fmt::arg("hint", *hint))
                               : say("pipeline.error.unknown_param", fmt::arg("block", type->id), fmt::arg("param", key))));
            }
            auto params = resolve_params(*type, decl.params);
            if (!params) fail(decl.line, 0, decl.id + ": " + params.error().message);
            if (decl.bypass && !bypassable(*type))
                fail(decl.line, 0, say("pipeline.graph.bypass_types", fmt::arg("node", decl.id), fmt::arg("block", type->id)));
            index_.emplace(decl.id, graph_.nodes.size());
            ids_.push_back(decl.id);
            graph_.nodes.push_back({decl.id, type, std::move(*params), std::vector<std::optional<PortRef>>(type->inputs.size()),
                                    std::vector<std::optional<Source>>(type->inputs.size()), decl.line, decl.bypass});
        }
    }

    std::size_t find_node(const PortRef& ref) const {
        if (const auto it = index_.find(ref.node); it != index_.end()) return it->second;
        const auto hint = nearest_name(ref.node, ids_);
        fail(ref.line, ref.column, hint ? say("pipeline.graph.unknown_node_hint", fmt::arg("node", ref.node), fmt::arg("hint", *hint))
                                        : say("pipeline.graph.unknown_node", fmt::arg("node", ref.node)));
    }

    std::pair<Source, PortType> output(const PortRef& ref, bool allow_input) const {
        const auto i = find_node(ref);
        const auto& n = graph_.nodes[i];
        const auto& outs = n.type->outputs;
        if (ref.port.empty()) {
            if (outs.empty()) fail(ref.line, ref.column, say("pipeline.graph.no_outputs", fmt::arg("node", n.id)));
            if (outs.size() > 1) {
                std::vector<std::string> names;
                for (const auto& p : outs) names.push_back(p.name);
                fail(ref.line, ref.column, say("pipeline.graph.need_output", fmt::arg("node", n.id), fmt::arg("ports", join(names)), fmt::arg("first", outs.front().name)));
            }
            return {{i, 0, {}}, outs.front().type};
        }
        for (std::size_t k = 0; k < outs.size(); ++k)
            if (outs[k].name == ref.port) return {{i, k, {}}, outs[k].type};
        const auto coeffs = std::find_if(outs.begin(), outs.end(), [](const PortSpec& p) { return p.type == PortType::coeffs; });
        if (coeffs != outs.end() && band_name(ref.port)) return {{i, static_cast<std::size_t>(coeffs - outs.begin()), ref.port}, PortType::signal};
        if (allow_input)
            for (std::size_t k = 0; k < n.type->inputs.size(); ++k)
                if (n.type->inputs[k].name == ref.port) {
                    if (!n.sources[k]) fail(ref.line, ref.column, say("pipeline.graph.missing_input", fmt::arg("node", n.id), fmt::arg("port", ref.port)));
                    return {*n.sources[k], n.type->inputs[k].type};
                }
        std::vector<std::string> names;
        for (const auto& p : outs) names.push_back(p.name);
        if (coeffs != outs.end()) names.emplace_back("a, d1…dJ");
        fail(ref.line, ref.column, say("pipeline.graph.unknown_output", fmt::arg("node", n.id), fmt::arg("port", ref.port), fmt::arg("ports", join(names))));
    }

    std::size_t input(const PortRef& ref, std::size_t node) const {
        const auto& n = graph_.nodes[node];
        const auto& ins = n.type->inputs;
        std::vector<std::string> names;
        for (const auto& p : ins) names.push_back(p.name);
        if (ins.empty()) fail(ref.line, ref.column, say("pipeline.graph.no_inputs", fmt::arg("node", n.id)));
        if (ref.port.empty()) {
            if (ins.size() == 1) return 0;
            fail(ref.line, ref.column, say("pipeline.graph.need_port", fmt::arg("node", n.id), fmt::arg("ports", join(names))));
        }
        for (std::size_t k = 0; k < ins.size(); ++k)
            if (ins[k].name == ref.port) return k;
        fail(ref.line, ref.column, say("pipeline.graph.unknown_input", fmt::arg("node", n.id), fmt::arg("port", ref.port), fmt::arg("ports", join(names))));
    }

    void add_wires() {
        for (const auto& edge : doc_.edges) {
            const auto [source, type] = output(edge.from, false);
            const auto to = find_node(edge.to);
            const auto k = input(edge.to, to);
            auto& n = graph_.nodes[to];
            const auto& want = n.type->inputs[k];
            if (want.type != type) {
                std::string message = say("pipeline.graph.type_mismatch", fmt::arg("from", ref_text(edge.from)), fmt::arg("from_type", type_name(type)),
                                          fmt::arg("to", n.id + "." + want.name), fmt::arg("to_type", type_name(want.type)));
                if (type == PortType::coeffs && want.type == PortType::signal) message += say("pipeline.graph.insert", fmt::arg("block", "idwt"));
                if (type == PortType::signal && want.type == PortType::coeffs) message += say("pipeline.graph.insert", fmt::arg("block", "dwt"));
                fail(edge.to.line, edge.to.column, std::move(message));
            }
            if (n.inputs[k])
                fail(edge.to.line, edge.to.column,
                     say("pipeline.graph.second_wire", fmt::arg("to", n.id + "." + want.name), fmt::arg("from", ref_text(*n.inputs[k])),
                         fmt::arg("line", n.inputs[k]->line)));
            n.inputs[k] = edge.from;
            n.sources[k] = source;
            wires_.push_back({source.node, to, edge.from.line});
        }
    }

    void check_inputs() const {
        for (const auto& n : graph_.nodes)
            for (std::size_t k = 0; k < n.inputs.size(); ++k)
                if (!n.inputs[k] && !n.type->inputs[k].optional)
                    fail(n.line, 0, say("pipeline.graph.missing_input", fmt::arg("node", n.id), fmt::arg("port", n.type->inputs[k].name)));
    }

    void sort() {
        const auto count = graph_.nodes.size();
        std::vector<std::size_t> pending(count, 0);
        std::vector<std::vector<const Wire*>> next(count);
        for (const auto& w : wires_) {
            ++pending[w.to];
            next[w.from].push_back(&w);
        }
        std::set<std::size_t> ready;
        for (std::size_t i = 0; i < count; ++i)
            if (pending[i] == 0) ready.insert(i);
        while (!ready.empty()) {
            const auto i = *ready.begin();
            ready.erase(ready.begin());
            graph_.order.push_back(i);
            for (const auto* w : next[i])
                if (--pending[w->to] == 0) ready.insert(w->to);
        }
        if (graph_.order.size() == count) return;
        enum class Mark { fresh, open, closed };
        std::vector<Mark> mark(count, Mark::fresh);
        std::vector<std::size_t> path;
        const std::function<void(std::size_t)> visit = [&](std::size_t i) {
            mark[i] = Mark::open;
            path.push_back(i);
            for (const auto* w : next[i]) {
                if (mark[w->to] == Mark::open) {
                    std::string cycle;
                    for (auto it = std::find(path.begin(), path.end(), w->to); it != path.end(); ++it) cycle += graph_.nodes[*it].id + " → ";
                    fail(w->line, 0, say("pipeline.graph.cycle", fmt::arg("cycle", cycle + graph_.nodes[w->to].id)));
                }
                if (mark[w->to] == Mark::fresh) visit(w->to);
            }
            path.pop_back();
            mark[i] = Mark::closed;
        };
        for (std::size_t i = 0; i < count; ++i)
            if (pending[i] > 0 && mark[i] == Mark::fresh) visit(i);
    }

    std::optional<std::size_t> levels_of(const Source& s) const {
        if (!s.band.empty()) return std::nullopt;
        const auto& n = graph_.nodes[s.node];
        if (n.type->id == "dwt") {
            const auto j = param_int(n.params, "levels");
            return j > 0 ? std::optional<std::size_t>{static_cast<std::size_t>(j)} : std::nullopt;
        }
        for (std::size_t k = 0; k < n.sources.size(); ++k)
            if (n.sources[k] && n.type->inputs[k].type == PortType::coeffs) return levels_of(*n.sources[k]);
        return std::nullopt;
    }

    void check_band(const Source& s, const PortRef& ref) const {
        if (s.band.empty()) return;
        const auto j = levels_of({s.node, s.output, {}});
        if (!j) return;
        const auto level = s.band == "a" ? *j : std::stoull(s.band.substr(1));
        const bool ok = s.band[0] == 'a' ? level == *j : level >= 1 && level <= *j;
        if (!ok)
            fail(ref.line, ref.column,
                 say("pipeline.graph.bad_band", fmt::arg("node", ref.node), fmt::arg("band", s.band), fmt::arg("levels", tr_count("unit.level", *j)), fmt::arg("last", "d" + std::to_string(*j))));
    }

    void check_bands() const {
        for (const auto& n : graph_.nodes)
            for (std::size_t k = 0; k < n.sources.size(); ++k)
                if (n.sources[k]) check_band(*n.sources[k], *n.inputs[k]);
    }

    void add_probes() {
        std::map<std::string, int, std::less<>> names;
        for (const auto& p : doc_.probes) {
            const auto source = output(p.at, true).first;
            check_band(source, p.at);
            auto name = p.name.empty() ? ref_text(p.at) : p.name;
            if (const auto it = names.find(name); it != names.end())
                fail(p.at.line, p.at.column, say("pipeline.graph.duplicate_probe", fmt::arg("name", name), fmt::arg("line", it->second)));
            names.emplace(name, p.at.line);
            graph_.probes.push_back({std::move(name), p.at, source});
        }
    }

    const PipelineDoc& doc_;
    BuildOptions options_;
    Graph graph_;
    std::map<std::string, std::size_t, std::less<>> index_;
    std::vector<std::string> ids_;
    std::vector<Wire> wires_;
};

}  // namespace

std::expected<Graph, SigError> build_graph(const PipelineDoc& doc, std::filesystem::path base_dir, BuildOptions options) {
    try {
        return Builder{doc, std::move(base_dir), options}.run();
    } catch (const Failure& f) {
        return std::unexpected{f.error};
    }
}

}  // namespace signals::pipeline
