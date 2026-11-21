#include "learn/structure.hpp"

#include <algorithm>
#include <functional>
#include <fmt/format.h>
#include <map>
#include <set>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/flatten.hpp"

namespace signals::learn {

namespace {

const pipeline::NodeDecl* node_of(const pipeline::PipelineDoc& doc, std::string_view id) {
    for (const auto& n : doc.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

bool reachable(const pipeline::PipelineDoc& doc, std::string_view from, std::string_view to, const std::set<std::string, std::less<>>& blocked) {
    std::set<std::string, std::less<>> seen{std::string{from}};
    std::vector<std::string> stack{std::string{from}};
    while (!stack.empty()) {
        const auto at = stack.back();
        stack.pop_back();
        if (at == to) return true;
        for (const auto& e : doc.edges)
            if (e.from.node == at && !seen.contains(e.to.node) && (e.to.node == to || !blocked.contains(e.to.node))) {
                seen.insert(e.to.node);
                stack.push_back(e.to.node);
            }
    }
    return false;
}

pipeline::Params resolved(const pipeline::NodeDecl& n, std::string_view input_node) {
    auto params = n.params;
    if (const auto* type = pipeline::find_block_type(n.type))
        if (auto r = pipeline::resolve_params(*type, n.params)) params = std::move(*r);
    if (n.id == input_node) params.erase("values");
    return params;
}

}  // namespace

NodeSet live_nodes(const pipeline::PipelineDoc& doc, std::span<const std::string> outputs) {
    NodeSet live{outputs.begin(), outputs.end()};
    for (const auto& out : outputs)
        if (!node_of(doc, out))
            for (const auto& n : doc.nodes)
                if (n.id.starts_with(out + "/")) live.insert(n.id);
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& e : doc.edges)
            if (live.contains(e.to.node) && !live.contains(e.from.node)) grew = live.insert(e.from.node).second;
    }
    return live;
}

std::size_t count_blocks(const pipeline::PipelineDoc& doc, std::string_view block, const NodeSet* live) {
    return static_cast<std::size_t>(
        std::ranges::count_if(doc.nodes, [&](const auto& n) { return n.type == block && !n.bypass && (!live || live->contains(n.id)); }));
}

bool path_exists(const pipeline::PipelineDoc& doc, std::string_view from, std::string_view to) {
    return node_of(doc, from) && node_of(doc, to) && reachable(doc, from, to, {});
}

bool all_paths_through(const pipeline::PipelineDoc& doc, const Through& t) {
    if (!node_of(doc, t.from) || !node_of(doc, t.to) || !reachable(doc, t.from, t.to, {})) return false;
    std::set<std::string, std::less<>> blocked;
    if (const auto* named = node_of(doc, t.block); named && named->id != t.from && named->id != t.to) blocked.insert(named->id);
    else
        for (const auto& n : doc.nodes)
            if (n.type == t.block && !n.bypass && n.id != t.from && n.id != t.to) blocked.insert(n.id);
    return !reachable(doc, t.from, t.to, blocked);
}

std::pair<std::string, std::size_t> most_called_block(const pipeline::PipelineDoc& doc) {
    std::map<std::string, const pipeline::BlockDef*, std::less<>> defs;
    for (const auto* list : {&doc.imported, &doc.blocks})
        for (const auto& b : *list) defs.insert_or_assign(b.name, &b);
    const auto calls_in = [](const std::vector<pipeline::NodeDecl>& nodes, std::string_view name) {
        return static_cast<std::size_t>(std::ranges::count_if(nodes, [&](const pipeline::NodeDecl& n) { return n.type == name && !n.bypass; }));
    };
    std::map<std::string, std::size_t, std::less<>> memo;
    std::set<std::string, std::less<>> open;
    const std::function<std::size_t(const std::string&)> instances = [&](const std::string& name) -> std::size_t {
        if (const auto it = memo.find(name); it != memo.end()) return it->second;
        if (!open.insert(name).second) return 0;
        auto total = calls_in(doc.nodes, name);
        for (const auto& [caller, def] : defs)
            if (const auto inside = calls_in(def->nodes, name); inside > 0) total += inside * instances(caller);
        open.erase(name);
        return memo[name] = total;
    };
    std::pair<std::string, std::size_t> best{{}, 0};
    for (const auto& [name, def] : defs)
        if (const auto n = instances(name); n > best.second) best = {name, n};
    return best;
}

std::pair<std::string, std::size_t> most_used_variable(const pipeline::PipelineDoc& doc) {
    std::pair<std::string, std::size_t> best{{}, 0};
    for (const auto& l : doc.lets) {
        const auto mentions = [&](std::string_view value) {
            const auto names = pipeline::variables_in(value);
            return std::ranges::find(names, l.name) != names.end();
        };
        std::size_t uses = 0;
        for (const auto& n : doc.nodes)
            for (const auto& [key, value] : n.params) uses += mentions(value);
        for (const auto& other : doc.lets) uses += mentions(other.value);
        if (uses > best.second) best = {l.name, uses};
    }
    return best;
}

std::vector<std::string> locked_changes(const pipeline::PipelineDoc& start, const pipeline::PipelineDoc& work, std::span<const std::string> locked,
                                        std::string_view input_node) {
    std::vector<std::string> out;
    const auto flat_start = pipeline::flatten(start);
    const auto flat_work = pipeline::flatten(work);
    const auto& before_doc = flat_start ? *flat_start : start;
    const auto& after_doc = flat_work ? *flat_work : work;
    for (const auto& id : locked) {
        const auto* before = node_of(before_doc, id);
        if (!before) continue;
        const auto* top = node_of(work, id);
        const auto* after = node_of(after_doc, id);
        if (!top || !after) {
            const auto inner = "/" + id;
            const bool moved = top || std::ranges::any_of(after_doc.nodes, [&](const pipeline::NodeDecl& n) { return n.id.ends_with(inner); });
            out.push_back(fmt::format(fmt::runtime(tr(moved ? "learn.check.locked_moved" : "learn.check.locked_missing")), fmt::arg("node", id)));
        } else if (after->type != before->type) {
            out.push_back(fmt::format(fmt::runtime(tr("learn.check.locked_type")), fmt::arg("node", id), fmt::arg("type", before->type)));
        } else if (after->bypass != before->bypass || resolved(*after, input_node) != resolved(*before, input_node)) {
            out.push_back(fmt::format(fmt::runtime(tr("learn.check.locked_params")), fmt::arg("node", id)));
        }
    }
    return out;
}

}  // namespace signals::learn
