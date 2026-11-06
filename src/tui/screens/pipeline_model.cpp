#include "tui/screens/pipeline_model.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kUndoLimit = 100;

core::Error sig_failure(const std::filesystem::path& file, const pipeline::SigError& e) {
    std::string where = file.string();
    if (e.line > 0) where += ":" + std::to_string(e.line);
    if (e.column > 0) where += ":" + std::to_string(e.column);
    return {core::Errc::invalid_argument, where + ": " + e.message};
}

}  // namespace

bool same_port(const pipeline::PortRef& a, const pipeline::PortRef& b) { return a.node == b.node && a.port == b.port; }

bool same_wire(const pipeline::Edge& a, const pipeline::Edge& b) { return same_port(a.from, b.from) && same_port(a.to, b.to); }

PipelineModel::PipelineModel() { rebuild(); }

std::filesystem::path PipelineModel::base_dir() const {
    if (path_) return std::filesystem::absolute(*path_).parent_path();
    std::error_code ec;
    auto here = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{"."} : here;
}

void PipelineModel::rebuild() {
    auto partial = pipeline::build_graph(doc_, base_dir(), {.partial = true});
    graph_.reset();
    problem_.reset();
    if (partial) graph_ = std::move(*partial);
    else problem_ = partial.error();
    if (doc_.nodes.empty()) problem_ = pipeline::SigError{0, 0, tr("tui.pipeline.empty")};
    else if (graph_)
        if (auto full = pipeline::build_graph(doc_, base_dir()); !full) problem_ = full.error();
}

core::Result<void> PipelineModel::commit(pipeline::PipelineDoc next) {
    if (auto check = pipeline::build_graph(next, base_dir(), {.partial = true}); !check) return core::make_error(core::Errc::invalid_argument, check.error().message);
    undo_.push_back(std::move(doc_));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
    doc_ = std::move(next);
    dirty_ = true;
    rebuild();
    return {};
}

core::Result<void> PipelineModel::load(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) return core::make_error(core::Errc::io_error, fmt::format(fmt::runtime(tr("pipeline.cli.cannot_read")), fmt::arg("path", file.string())));
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    auto doc = pipeline::parse_sig(text);
    if (!doc) return std::unexpected{sig_failure(file, doc.error())};
    undo_.push_back(std::move(doc_));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
    doc_ = std::move(*doc);
    path_ = file;
    dirty_ = false;
    selection = {};
    rebuild();
    return {};
}

core::Result<void> PipelineModel::save(const std::filesystem::path& file) {
    const auto from = base_dir();
    const auto to = std::filesystem::absolute(file).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(to, ec);
    for (auto& n : doc_.nodes) {
        const auto* type = pipeline::find_block_type(n.type);
        if (!type) continue;
        for (const auto& spec : type->params) {
            if (spec.kind != pipeline::ParamSpec::Kind::path) continue;
            const auto it = n.params.find(spec.name);
            if (it == n.params.end()) continue;
            const auto value = pipeline::utf8_path(it->second);
            if (value.is_absolute()) continue;
            const auto target = std::filesystem::weakly_canonical(from / value, ec);
            const auto moved = std::filesystem::relative(target, std::filesystem::weakly_canonical(to, ec), ec);
            if (!ec && !moved.empty()) {
                const auto text = moved.generic_u8string();
                it->second.assign(reinterpret_cast<const char*>(text.data()), text.size());
            }
        }
    }
    std::ofstream out{file, std::ios::binary};
    out << pipeline::write_sig(doc_);
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + file.string());
    path_ = file;
    dirty_ = false;
    rebuild();
    return {};
}

std::string PipelineModel::add_node(std::string_view type) {
    std::string id{type};
    for (int k = 2; std::any_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == id; }); ++k) id = std::string{type} + std::to_string(k);
    auto next = doc_;
    next.nodes.push_back({id, std::string{type}, {}, 0, false});
    if (!commit(std::move(next))) return {};
    return id;
}

core::Result<void> PipelineModel::insert_into_wire(const pipeline::Edge& wire, std::string_view type) {
    std::string id{type};
    for (int k = 2; std::any_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == id; }); ++k) id = std::string{type} + std::to_string(k);
    auto next = doc_;
    const auto it = std::find_if(next.edges.begin(), next.edges.end(), [&](const auto& e) { return same_wire(e, wire); });
    if (it == next.edges.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_wire"));
    const auto position = it - next.edges.begin();
    next.edges.erase(it);
    next.nodes.push_back({id, std::string{type}, {}, 0, false});
    const pipeline::PortRef middle{id, {}, 0, 0};
    pipeline::Edge in_edge, out_edge;
    in_edge.from = wire.from;
    in_edge.to = middle;
    out_edge.from = middle;
    out_edge.to = wire.to;
    next.edges.insert(next.edges.begin() + position, in_edge);
    next.edges.insert(next.edges.begin() + position + 1, out_edge);
    if (auto ok = commit(std::move(next)); !ok) return ok;
    selection = {Selection::Kind::node, id, {}};
    return {};
}

core::Result<void> PipelineModel::connect(const pipeline::PortRef& from, const pipeline::PortRef& to) {
    auto next = doc_;
    next.edges.push_back({from, to});
    return commit(std::move(next));
}

void PipelineModel::disconnect(const pipeline::Edge& wire) {
    auto next = doc_;
    std::erase_if(next.edges, [&](const auto& e) { return same_wire(e, wire); });
    (void)commit(std::move(next));
}

void PipelineModel::remove_node(std::string_view id) {
    auto next = doc_;
    std::erase_if(next.nodes, [&](const auto& n) { return n.id == id; });
    std::erase_if(next.edges, [&](const auto& e) { return e.from.node == id || e.to.node == id; });
    std::erase_if(next.probes, [&](const auto& p) { return p.at.node == id; });
    if (selection.node == id || selection.wire.from.node == id || selection.wire.to.node == id) selection = {};
    (void)commit(std::move(next));
}

core::Result<void> PipelineModel::set_param(std::string_view node, std::string_view param, std::string_view value) {
    auto next = doc_;
    if (auto ok = pipeline::apply_set(next, std::string{node} + "." + std::string{param} + "=" + std::string{value}); !ok) return ok;
    return commit(std::move(next));
}

core::Result<void> PipelineModel::toggle_bypass(std::string_view node) {
    auto next = doc_;
    const auto it = std::find_if(next.nodes.begin(), next.nodes.end(), [&](const auto& n) { return n.id == node; });
    if (it == next.nodes.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
    it->bypass = !it->bypass;
    return commit(std::move(next));
}

void PipelineModel::toggle_probe(const pipeline::PortRef& at) {
    auto next = doc_;
    const auto before = next.probes.size();
    std::erase_if(next.probes, [&](const auto& p) { return same_port(p.at, at); });
    if (next.probes.size() == before) next.probes.push_back({at, {}});
    (void)commit(std::move(next));
}

bool PipelineModel::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(doc_));
    doc_ = std::move(undo_.back());
    undo_.pop_back();
    dirty_ = true;
    rebuild();
    return true;
}

bool PipelineModel::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(doc_));
    doc_ = std::move(redo_.back());
    redo_.pop_back();
    dirty_ = true;
    rebuild();
    return true;
}

}  // namespace signals::tui
