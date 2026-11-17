#include "pipeline/flatten.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <fmt/format.h>

#include "core/expr.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {
namespace {

using Kind = ParamSpec::Kind;

constexpr std::size_t kMaxExpanded = 64 * 1024;
constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxTotal = 8 * 1024 * 1024;

bool name_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool name_char(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

bool plain_number(std::string_view s) {
    if (s.empty() || s.front() == ' ') return false;
    const std::string text{s};
    char* end = nullptr;
    std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

bool numeric(Kind k) { return k == Kind::integer || k == Kind::real; }
bool textual(Kind k) { return k == Kind::text || k == Kind::path || k == Kind::gains; }

core::Error fault(std::string message) { return {core::Errc::invalid_argument, std::move(message)}; }

std::string unknown(std::string_view name, const Scope& scope) {
    std::vector<std::string> names;
    for (const auto& [n, v] : scope.values) names.push_back(n);
    const auto hint = nearest_name(name, names);
    return hint ? fmt::format(fmt::runtime(tr("pipeline.vars.unknown_hint")), fmt::arg("name", name), fmt::arg("hint", *hint))
                : fmt::format(fmt::runtime(tr("pipeline.vars.unknown")), fmt::arg("name", name));
}

struct Memo {
    std::string text;
    std::size_t depth = 0;
};

struct Expander {
    explicit Expander(const Scope& s) : scope{s} {}

    const Scope& scope;
    std::size_t budget = kMaxTotal;
    std::map<Kind, std::map<std::string, Memo, std::less<>>> memo;
    std::vector<std::string> stack;

    core::Result<const Memo*> resolve(const std::string& name, Kind kind);
    core::Result<std::string> expand(std::string_view value, Kind kind, std::size_t& depth);
};

core::Error too_deep() { return fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_deep")), fmt::arg("limit", kMaxDepth))); }

core::Result<const Memo*> Expander::resolve(const std::string& name, Kind kind) {
    auto& table = memo[kind];
    if (const auto hit = table.find(name); hit != table.end()) {
        if (stack.size() + hit->second.depth > kMaxDepth) return std::unexpected{too_deep()};
        return &hit->second;
    }
    const auto it = scope.values.find(name);
    if (it == scope.values.end()) return std::unexpected{fault(unknown(name, scope))};
    if (stack.size() >= kMaxDepth) return std::unexpected{too_deep()};
    if (std::ranges::find(stack, name) != stack.end()) {
        std::string chain;
        for (const auto& s : stack) chain += s + " → ";
        return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.cycle")), fmt::arg("chain", chain + name)))};
    }
    stack.push_back(name);
    std::size_t depth = 0;
    auto text = expand(it->second, kind, depth);
    stack.pop_back();
    if (!text) return std::unexpected{text.error()};
    return &table.emplace(name, Memo{std::move(*text), depth + 1}).first->second;
}

core::Result<std::string> Expander::expand(std::string_view value, Kind kind, std::size_t& depth) {
    std::string out;
    std::size_t names = 0, at = 0;
    bool other = false;
    const std::string* only = nullptr;
    while (at < value.size()) {
        if (value[at] != '$') {
            out += value[at++];
            other = true;
            continue;
        }
        if (at + 1 < value.size() && value[at + 1] == '$') {
            out += '$';
            at += 2;
            other = true;
            continue;
        }
        auto end = at + 1;
        if (end >= value.size() || !name_start(value[end])) return std::unexpected{fault(tr("pipeline.vars.bad_dollar"))};
        while (end < value.size() && name_char(value[end])) ++end;
        const auto entry = resolve(std::string{value.substr(at + 1, end - at - 1)}, kind);
        if (!entry) return std::unexpected{entry.error()};
        const auto& inner = (*entry)->text;
        depth = std::max(depth, (*entry)->depth);
        ++names;
        only = &inner;
        const bool wrap = numeric(kind) && !plain_number(inner);
        const auto size = inner.size() + (wrap ? 2 : 0);
        if (size > budget) return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_much")), fmt::arg("limit", kMaxTotal)))};
        budget -= size;
        if (wrap) out += '(';
        out += inner;
        if (wrap) out += ')';
        if (out.size() > kMaxExpanded) return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_long")), fmt::arg("limit", kMaxExpanded)))};
        at = end;
    }
    if (names == 1 && !other) return *only;
    if (names > 0 && !numeric(kind) && !textual(kind)) return std::unexpected{fault(tr("pipeline.vars.expr_kind"))};
    return out;
}

}  // namespace

std::expected<Scope, SigError> resolve_lets(std::span<const LetDecl> lets) {
    Scope scope;
    for (const auto& l : lets) scope.values.emplace(l.name, l.value);
    Expander expander{scope};
    for (const auto& l : lets)
        if (auto r = expander.resolve(l.name, Kind::real); !r) return std::unexpected{SigError{l.line, 0, "let " + l.name + ": " + r.error().message}};
    return scope;
}

core::Result<std::string> substitute(std::string_view value, const Scope& scope, Kind kind) {
    Expander expander{scope};
    std::size_t depth = 0;
    return expander.expand(value, kind, depth);
}

std::vector<std::string> variables_in(std::string_view value) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < value.size(); ++at) {
        if (value[at] != '$') continue;
        if (at + 1 < value.size() && value[at + 1] == '$') {
            ++at;
            continue;
        }
        if (at + 1 >= value.size() || !name_start(value[at + 1])) continue;
        auto end = at + 1;
        while (end < value.size() && name_char(value[end])) ++end;
        std::string name{value.substr(at + 1, end - at - 1)};
        if (std::ranges::find(out, name) == out.end()) out.push_back(std::move(name));
        at = end - 1;
    }
    return out;
}

std::vector<LetValue> let_values(std::span<const LetDecl> lets) {
    std::vector<LetValue> out;
    const auto scope = resolve_lets(lets);
    for (const auto& l : lets) {
        LetValue v{l.name, l.value, std::nullopt, l.value};
        if (scope) {
            if (const auto expr = substitute("$" + l.name, *scope, Kind::real))
                if (const auto number = core::eval_number(*expr)) v.number = *number;
            if (auto text = substitute("$" + l.name, *scope, Kind::text)) v.expanded = std::move(*text);
        }
        out.push_back(std::move(v));
    }
    return out;
}

namespace {

constexpr std::size_t kMaxBlockDepth = 16;

struct CallPorts {
    std::vector<std::string> ins, outs;
};

struct Expansion {
    std::map<std::string, const BlockDef*, std::less<>> defs;
    std::vector<NodeDecl> nodes;
    std::vector<Edge> edges;
    std::vector<ProbeDecl> probes;
    std::set<std::string, std::less<>> pseudo;
    std::vector<std::string> stack;
};

struct Frame {
    const BlockDef* def = nullptr;
    std::string prefix, instance;
    int call_line = 0;
    std::size_t depth = 0;
};

[[noreturn]] void stop(int line, std::string message) { throw SigError{line, 0, std::move(message)}; }

std::string inside(const Frame& f, int line, const std::string& message) {
    if (!f.def) return message;
    return fmt::format(fmt::runtime(tr("pipeline.blocks.inside")), fmt::arg("instance", f.instance), fmt::arg("block", f.def->name), fmt::arg("line", line),
                       fmt::arg("message", message));
}

void check_ports(const BlockDef& def) {
    for (const auto& in : def.ins)
        if (std::ranges::none_of(def.edges, [&](const Edge& e) { return e.from.node == in.name; }))
            stop(in.line, fmt::format(fmt::runtime(tr("pipeline.blocks.unused_input")), fmt::arg("block", def.name), fmt::arg("port", in.name)));
    for (const auto& out : def.outs)
        if (std::ranges::count_if(def.edges, [&](const Edge& e) { return e.to.node == out.name; }) != 1)
            stop(out.line, fmt::format(fmt::runtime(tr("pipeline.blocks.output_wires")), fmt::arg("block", def.name), fmt::arg("port", out.name)));
}

std::string port_of(const std::vector<std::string>& ports, const std::string& asked, const std::string& call, int line) {
    if (!asked.empty()) {
        if (std::ranges::find(ports, asked) == ports.end()) {
            std::string list;
            for (const auto& p : ports) list += (list.empty() ? "" : ", ") + p;
            stop(line, fmt::format(fmt::runtime(tr("pipeline.blocks.unknown_port")), fmt::arg("call", call), fmt::arg("port", asked), fmt::arg("ports", list)));
        }
        return asked;
    }
    if (ports.size() != 1) {
        std::string list;
        for (const auto& p : ports) list += (list.empty() ? "" : ", ") + p;
        stop(line, fmt::format(fmt::runtime(tr("pipeline.blocks.port_needed")), fmt::arg("call", call), fmt::arg("ports", list)));
    }
    return ports.front();
}

template <class Body>
void expand_body(Expansion& x, const Body& body, const Scope& scope, const Frame& f) {
    std::map<std::string, CallPorts, std::less<>> calls;
    Expander expander{scope};
    const auto own_port = [&](const std::vector<PortDecl>& ports, const std::string& name) {
        return f.def && std::ranges::any_of(ports, [&](const PortDecl& p) { return p.name == name; });
    };
    for (const auto& n : body.nodes) {
        const int line = f.def ? f.call_line : n.line;
        const auto call = x.defs.find(n.type);
        if (call == x.defs.end()) {
            NodeDecl out = n;
            out.id = f.prefix + n.id;
            out.source = n.params;
            out.instance = f.instance;
            out.block = f.def ? f.def->name : std::string{};
            out.def_line = f.def ? n.line : 0;
            out.line = line;
            const auto* type = find_block_type(n.type);
            for (auto& [param, value] : out.params) {
                Kind kind = Kind::text;
                if (type)
                    if (const auto spec = std::ranges::find_if(type->params, [&](const ParamSpec& p) { return p.name == param; }); spec != type->params.end()) kind = spec->kind;
                std::size_t depth = 0;
                auto done = expander.expand(value, kind, depth);
                if (!done) stop(line, inside(f, n.line, n.id + "." + param + ": " + done.error().message));
                value = std::move(*done);
                if (kind == Kind::path && f.def && !f.def->origin.empty() && !value.empty()) {
                    const auto p = utf8_path(value);
                    if (p.is_relative()) {
                        const auto full = (f.def->origin.parent_path() / p).lexically_normal().generic_u8string();
                        value.assign(full.begin(), full.end());
                    }
                }
            }
            x.nodes.push_back(std::move(out));
            continue;
        }
        const auto& def = *call->second;
        if (std::ranges::find(x.stack, def.name) != x.stack.end()) {
            std::string chain;
            for (const auto& s : x.stack) chain += s + " → ";
            stop(line, inside(f, n.line, fmt::format(fmt::runtime(tr("pipeline.blocks.recursion")), fmt::arg("chain", chain + def.name))));
        }
        if (f.depth + 1 > kMaxBlockDepth) stop(line, inside(f, n.line, fmt::format(fmt::runtime(tr("pipeline.blocks.too_deep")), fmt::arg("limit", kMaxBlockDepth))));
        check_ports(def);
        Scope inner;
        for (const auto& [key, value] : n.params) {
            if (!def.defaults.contains(key)) {
                std::vector<std::string> names{def.order.begin(), def.order.end()};
                const auto hint = nearest_name(key, names);
                stop(line, inside(f, n.line, hint ? fmt::format(fmt::runtime(tr("pipeline.blocks.unknown_param_hint")), fmt::arg("block", def.name), fmt::arg("param", key), fmt::arg("hint", *hint))
                                                  : fmt::format(fmt::runtime(tr("pipeline.blocks.unknown_param")), fmt::arg("block", def.name), fmt::arg("param", key))));
            }
            std::size_t depth = 0;
            auto done = expander.expand(value, Kind::text, depth);
            if (!done) stop(line, inside(f, n.line, n.id + "." + key + ": " + done.error().message));
            inner.values.emplace(key, escape_dollars(*done));
        }
        for (const auto& [key, value] : def.defaults) inner.values.emplace(key, value);
        for (const auto& l : def.lets)
            if (!inner.values.emplace(l.name, l.value).second)
                stop(l.line, fmt::format(fmt::runtime(tr("pipeline.blocks.let_param")), fmt::arg("block", def.name), fmt::arg("name", l.name)));
        CallPorts ports;
        for (const auto& p : def.ins) ports.ins.push_back(p.name);
        for (const auto& p : def.outs) ports.outs.push_back(p.name);
        const auto base = f.prefix + n.id + "/";
        for (const auto& p : ports.ins) x.pseudo.insert(base + p);
        for (const auto& p : ports.outs) x.pseudo.insert(base + p);
        calls.emplace(n.id, ports);
        if (n.bypass) {
            if (ports.ins.size() != 1 || ports.outs.size() != 1)
                stop(line, inside(f, n.line, fmt::format(fmt::runtime(tr("pipeline.blocks.bypass")), fmt::arg("node", n.id), fmt::arg("block", def.name))));
            x.edges.push_back({{base + ports.ins.front(), {}, n.line, 0}, {base + ports.outs.front(), {}, n.line, 0}});
            continue;
        }
        x.stack.push_back(def.name);
        expand_body(x, def, inner, Frame{&def, base, f.prefix + n.id, line, f.depth + 1});
        x.stack.pop_back();
    }
    const auto from_ref = [&](const PortRef& r) -> PortRef {
        if (f.def && own_port(f.def->ins, r.node)) return {f.prefix + r.node, {}, r.line, r.column};
        if (const auto c = calls.find(r.node); c != calls.end())
            return {f.prefix + r.node + "/" + port_of(c->second.outs, r.port, r.node, f.def ? f.call_line : r.line), {}, r.line, r.column};
        return {f.prefix + r.node, r.port, r.line, r.column};
    };
    const auto to_ref = [&](const PortRef& r) -> PortRef {
        if (f.def && own_port(f.def->outs, r.node)) return {f.prefix + r.node, {}, r.line, r.column};
        if (const auto c = calls.find(r.node); c != calls.end())
            return {f.prefix + r.node + "/" + port_of(c->second.ins, r.port, r.node, f.def ? f.call_line : r.line), {}, r.line, r.column};
        return {f.prefix + r.node, r.port, r.line, r.column};
    };
    for (const auto& e : body.edges) x.edges.push_back({from_ref(e.from), to_ref(e.to), e.comments});
    for (const auto& p : body.probes) {
        ProbeDecl out = p;
        out.at = from_ref(p.at);
        if (f.def) out.name = f.instance + "/" + (p.name.empty() ? p.at.node : p.name);
        x.probes.push_back(std::move(out));
    }
}

void contract(Expansion& x) {
    const auto pending = [&] {
        return std::ranges::any_of(x.edges, [&](const Edge& e) { return x.pseudo.contains(e.from.node) || x.pseudo.contains(e.to.node); });
    };
    for (std::size_t pass = 0; pass <= x.pseudo.size() && pending(); ++pass)
        for (const auto& id : x.pseudo) {
        std::vector<Edge> in, out, kept;
        for (auto& e : x.edges) {
            if (e.to.node == id) in.push_back(std::move(e));
            else if (e.from.node == id) out.push_back(std::move(e));
            else kept.push_back(std::move(e));
        }
        for (const auto& a : in)
            for (const auto& b : out) kept.push_back({a.from, b.to, b.comments});
        x.edges = std::move(kept);
        for (auto& p : x.probes)
            if (p.at.node == id && in.size() == 1) p.at = in.front().from;
    }
}

}  // namespace

std::expected<PipelineDoc, SigError> flatten(const PipelineDoc& doc, const FlattenOptions&) {
    PipelineDoc out = doc;
    for (auto& n : out.nodes)
        if (n.source.empty()) n.source = n.params;
    if (doc.version < 2) return out;
    const auto scope = resolve_lets(doc.lets);
    if (!scope) return std::unexpected{scope.error()};
    Expansion x;
    for (const auto* list : {&doc.imported, &doc.blocks})
        for (const auto& b : *list) x.defs.insert_or_assign(b.name, &b);
    try {
        expand_body(x, doc, *scope, Frame{});
        contract(x);
    } catch (const SigError& e) {
        return std::unexpected{e};
    }
    out.lets.clear();
    out.blocks.clear();
    out.imported.clear();
    out.uses.clear();
    out.nodes = std::move(x.nodes);
    out.edges = std::move(x.edges);
    out.probes = std::move(x.probes);
    return out;
}

}  // namespace signals::pipeline
