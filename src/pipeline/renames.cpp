#include "pipeline/renames.hpp"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

#include "core/expr.hpp"
#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

const std::vector<Rename>& table() {
    static const std::vector<Rename> renames{};
    return renames;
}

std::string note(std::string_view where, std::string_view from, std::string_view to, std::string_view reason) {
    return fmt::format(fmt::runtime(tr("pipeline.migrate.rename")), fmt::arg("where", where), fmt::arg("from", from), fmt::arg("to", to), fmt::arg("reason", reason));
}

}  // namespace

std::span<const Rename> v1_renames() { return table(); }

std::string current_block_name(std::span<const Rename> renames, std::string_view v1_block) {
    for (const auto& r : renames)
        if (r.kind == Rename::Kind::block && r.from == v1_block) return r.to;
    return std::string{v1_block};
}

std::optional<std::string> renamed(std::span<const Rename> renames, Rename::Kind kind, std::string_view block, std::string_view from) {
    for (const auto& r : renames) {
        if (r.kind != kind || r.from != from) continue;
        if (kind == Rename::Kind::block || r.block == block || current_block_name(renames, r.block) == block) return r.to;
    }
    return std::nullopt;
}

std::string rename_expr_names(std::string_view expr, const std::map<std::string, std::string, std::less<>>& names) {
    const auto parsed = core::parse_expr(expr);
    if (!parsed) return std::string{expr};
    std::vector<std::pair<std::size_t, std::string>> hits;
    for (const auto& n : parsed->nodes)
        if (n.op == core::Expr::Op::name && names.contains(n.name) && expr.substr(n.pos, n.name.size()) == n.name) hits.emplace_back(n.pos, n.name);
    std::ranges::sort(hits, std::greater<>{});
    std::string out{expr};
    for (const auto& [pos, name] : hits) out.replace(pos, name.size(), names.find(name)->second);
    return out;
}

std::vector<std::string> apply_renames(PipelineDoc& doc, std::span<const Rename> renames) {
    std::vector<std::string> notes;
    std::map<std::string, std::string, std::less<>> type_of;
    for (const auto& n : doc.nodes) type_of.emplace(n.id, n.type);
    for (auto& node : doc.nodes)
        for (const auto& r : renames) {
            if (r.block != node.type) continue;
            if (r.kind == Rename::Kind::param) {
                const auto it = node.params.find(r.from);
                if (it == node.params.end() || node.params.contains(r.to)) continue;
                auto value = std::move(it->second);
                node.params.erase(it);
                node.params.emplace(r.to, std::move(value));
                notes.push_back(note(node.id, r.from, r.to, r.reason));
            } else if (r.kind == Rename::Kind::choice) {
                const auto it = node.params.find(r.param);
                if (it == node.params.end() || it->second != r.from) continue;
                it->second = r.to;
                notes.push_back(note(node.id + "." + r.param, r.from, r.to, r.reason));
            }
        }
    const auto port = [&](PortRef& ref) {
        const auto t = type_of.find(ref.node);
        if (t == type_of.end() || ref.port.empty()) return;
        for (const auto& r : renames)
            if (r.kind == Rename::Kind::port && r.block == t->second && r.from == ref.port) {
                notes.push_back(note(ref.node, ref.port, r.to, r.reason));
                ref.port = r.to;
                return;
            }
    };
    for (auto& e : doc.edges) port(e.from), port(e.to);
    for (auto& p : doc.probes) port(p.at);
    for (auto& node : doc.nodes) {
        const auto expr = node.params.find("expr");
        if (node.type != "assert" || expr == node.params.end()) continue;
        std::map<std::string, std::string, std::less<>> fields;
        for (const auto& e : doc.edges) {
            if (e.to.node != node.id) continue;
            const auto source = type_of.find(e.from.node);
            if (source == type_of.end()) continue;
            for (const auto& r : renames)
                if (r.kind == Rename::Kind::field && r.block == source->second) fields.emplace(e.to.port + "." + r.from, e.to.port + "." + r.to);
        }
        if (fields.empty()) continue;
        auto changed = rename_expr_names(expr->second, fields);
        if (changed == expr->second) continue;
        notes.push_back(note(node.id + ".expr", expr->second, changed, ""));
        expr->second = std::move(changed);
    }
    for (auto& node : doc.nodes)
        for (const auto& r : renames)
            if (r.kind == Rename::Kind::block && r.from == node.type) {
                notes.push_back(note(node.id, node.type, r.to, r.reason));
                node.type = r.to;
                break;
            }
    return notes;
}

}  // namespace signals::pipeline
