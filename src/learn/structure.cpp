#include "learn/structure.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <set>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

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

std::size_t count_blocks(const pipeline::PipelineDoc& doc, std::string_view block) {
    return static_cast<std::size_t>(std::ranges::count_if(doc.nodes, [&](const auto& n) { return n.type == block && !n.bypass; }));
}

bool path_exists(const pipeline::PipelineDoc& doc, std::string_view from, std::string_view to) {
    return node_of(doc, from) && node_of(doc, to) && reachable(doc, from, to, {});
}

bool all_paths_through(const pipeline::PipelineDoc& doc, const Through& t) {
    if (!node_of(doc, t.from) || !node_of(doc, t.to) || !reachable(doc, t.from, t.to, {})) return false;
    std::set<std::string, std::less<>> blocked;
    for (const auto& n : doc.nodes)
        if (n.type == t.block && !n.bypass && n.id != t.from && n.id != t.to) blocked.insert(n.id);
    return !reachable(doc, t.from, t.to, blocked);
}

std::vector<std::string> locked_changes(const pipeline::PipelineDoc& start, const pipeline::PipelineDoc& work, std::span<const std::string> locked,
                                        std::string_view input_node) {
    std::vector<std::string> out;
    for (const auto& id : locked) {
        const auto* before = node_of(start, id);
        if (!before) continue;
        const auto* after = node_of(work, id);
        if (!after) {
            out.push_back(fmt::format(fmt::runtime(tr("learn.check.locked_missing")), fmt::arg("node", id)));
        } else if (after->type != before->type) {
            out.push_back(fmt::format(fmt::runtime(tr("learn.check.locked_type")), fmt::arg("node", id), fmt::arg("type", before->type)));
        } else if (after->bypass != before->bypass || resolved(*after, input_node) != resolved(*before, input_node)) {
            out.push_back(fmt::format(fmt::runtime(tr("learn.check.locked_params")), fmt::arg("node", id)));
        }
    }
    return out;
}

}  // namespace signals::learn
