#include "tui/screens/pipeline_model.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/flatten.hpp"
#include "tui/widgets/format.hpp"
#include "tui/monitor/sampler.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kUndoLimit = 100;
constexpr std::string_view kInputProbe = "@input";

bool identifier(std::string_view name) {
    return !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_') &&
           std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

std::string ref_text(const pipeline::PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; }

const pipeline::NodeDecl* node_in(const std::vector<pipeline::NodeDecl>& nodes, std::string_view id) {
    const auto it = std::ranges::find(nodes, id, &pipeline::NodeDecl::id);
    return it == nodes.end() ? nullptr : &*it;
}

std::vector<std::string> instances_of(const pipeline::PipelineDoc& doc, std::string_view name) {
    std::vector<std::string> out;
    const std::function<void(const std::string&, const std::vector<pipeline::NodeDecl>&, int)> walk = [&](const std::string& prefix, const auto& nodes, int depth) {
        if (depth > 16) return;
        for (const auto& n : nodes) {
            const auto* def = pipeline::find_own_block(doc, n.type);
            if (!def || n.bypass) continue;
            if (def->name == name) out.push_back(prefix + n.id);
            walk(prefix + n.id + "/", def->nodes, depth + 1);
        }
    };
    walk("", doc.nodes, 0);
    return out;
}

bool plain_value(std::string_view v) {
    if (v.empty()) return false;
    char* end = nullptr;
    const std::string text{v};
    std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() || identifier(v);
}

std::string bind_params(std::string_view text, const std::map<std::string, std::string, std::less<>>& values, pipeline::ParamSpec::Kind kind, int depth = 0) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        if (text[at] != '$') {
            out += text[at++];
            continue;
        }
        if (at + 1 < text.size() && text[at + 1] == '$') {
            out += "$$";
            at += 2;
            continue;
        }
        auto end = at + 1;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
        const auto name = text.substr(at + 1, end - at - 1);
        const auto it = values.find(name);
        if (it == values.end() || depth > 16) {
            out += text.substr(at, end - at);
        } else {
            const auto value = bind_params(it->second, values, kind, depth + 1);
            const bool numeric = kind == pipeline::ParamSpec::Kind::integer || kind == pipeline::ParamSpec::Kind::real;
            const bool whole = at == 0 && end == text.size();
            out += numeric && !whole && !plain_value(value) ? "(" + value + ")" : value;
        }
        at = end;
    }
    return out;
}

pipeline::ParamSpec::Kind kind_of(const pipeline::NodeDecl& n, std::string_view param) {
    if (const auto* type = pipeline::find_block_type(n.type))
        if (const auto spec = std::ranges::find(type->params, param, &pipeline::ParamSpec::name); spec != type->params.end()) return spec->kind;
    return pipeline::ParamSpec::Kind::text;
}

core::Error sig_failure(const std::filesystem::path& file, const pipeline::SigError& e) {
    std::string where = file.string();
    if (e.line > 0) where += ":" + std::to_string(e.line);
    if (e.column > 0) where += ":" + std::to_string(e.column);
    return {core::Errc::invalid_argument, where + ": " + e.message};
}

}  // namespace

bool same_port(const pipeline::PortRef& a, const pipeline::PortRef& b) { return a.node == b.node && a.port == b.port; }

bool same_wire(const pipeline::Edge& a, const pipeline::Edge& b) { return same_port(a.from, b.from) && same_port(a.to, b.to); }

PipelineModel::PipelineModel() {
    const auto ram = physical_memory_bytes();
    cache_ = std::make_shared<pipeline::ResultCache>(ram > 0 ? static_cast<std::size_t>(ram / 4) : std::size_t{1} << 30);
    rebuild();
}

std::filesystem::path PipelineModel::base_dir() const {
    if (path_) return std::filesystem::absolute(*path_).parent_path();
    std::error_code ec;
    auto here = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{"."} : here;
}

void PipelineModel::rebuild() {
    auto partial = pipeline::build_graph(doc_, base_dir(), {.partial = true});
    graph_.reset();
    view_.reset();
    problem_.reset();
    if (partial) graph_ = std::move(*partial);
    else problem_ = partial.error();
    if (doc_.nodes.empty()) problem_ = pipeline::SigError{0, 0, tr("tui.pipeline.empty")};
    else if (graph_)
        if (auto full = pipeline::build_graph(doc_, base_dir()); !full) problem_ = full.error();
    if (!inside_.empty() && !inside_block()) inside_.clear();
    if (const auto* def = inside_.empty() ? nullptr : inside_block()) {
        view_doc_ = {};
        view_doc_.version = pipeline::kSigVersion;
        view_doc_.name = def->name;
        view_doc_.nodes = def->nodes;
        view_doc_.edges = def->edges;
        if (auto body = pipeline::build_block_graph(doc_, def->name, base_dir())) {
            const auto prefix = inside_ + "/";
            for (const auto& p : doc_.probes) {
                if (!p.at.node.starts_with(prefix)) continue;
                const auto local = p.at.node.substr(prefix.size());
                const auto it = std::ranges::find(body->nodes, local, &pipeline::GraphNode::id);
                if (it == body->nodes.end()) continue;
                const auto i = static_cast<std::size_t>(it - body->nodes.begin());
                std::size_t output = 0;
                for (std::size_t k = 0; k < it->type->outputs.size(); ++k)
                    if (it->type->outputs[k].name == p.at.port) output = k;
                body->probes.push_back({p.name.empty() ? ref_text(p.at) : p.name, {local, p.at.port, 0, 0}, {i, output, {}}});
            }
            view_ = std::move(*body);
        }
        return;
    }
    if (doc_.blocks.empty() && doc_.imported.empty()) view_ = graph_;
    else if (auto collapsed = pipeline::build_graph(doc_, base_dir(), {.partial = true, .collapse = true})) view_ = std::move(*collapsed);
    else view_ = graph_;
}

const pipeline::BlockDef* PipelineModel::inside_block() const {
    const pipeline::BlockDef* def = nullptr;
    const auto* nodes = &doc_.nodes;
    for (std::size_t at = 0; at <= inside_.size() && !inside_.empty();) {
        const auto end = std::min(inside_.find('/', at), inside_.size());
        const auto* n = node_in(*nodes, std::string_view{inside_}.substr(at, end - at));
        if (!n) return nullptr;
        def = pipeline::find_own_block(doc_, n->type);
        if (!def) return nullptr;
        nodes = &def->nodes;
        at = end + 1;
    }
    return def;
}

std::size_t PipelineModel::calls_of(std::string_view block) const { return instances_of(doc_, block).size(); }

core::Result<void> PipelineModel::change(const std::function<core::Result<void>(pipeline::PipelineDoc& body, pipeline::PipelineDoc& doc)>& edit) {
    auto next = doc_;
    if (inside_.empty()) {
        if (auto ok = edit(next, next); !ok) return ok;
        return commit(std::move(next));
    }
    const auto* current = inside_block();
    if (!current) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
    const auto def = std::ranges::find(next.blocks, current->name, &pipeline::BlockDef::name);
    if (def == next.blocks.end())
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.pipeline.block.readonly")), fmt::arg("name", current->name),
                                                                          fmt::arg("file", current->origin.filename().string())));
    pipeline::PipelineDoc body;
    body.version = pipeline::kSigVersion;
    for (const auto& key : def->order) body.lets.push_back({key, def->defaults.at(key)});
    body.lets.insert(body.lets.end(), def->lets.begin(), def->lets.end());
    body.nodes = def->nodes;
    body.edges = def->edges;
    body.probes = def->probes;
    body.blocks = next.blocks;
    body.imported = next.imported;
    if (auto ok = edit(body, next); !ok) return ok;
    const auto again = std::ranges::find(next.blocks, current->name, &pipeline::BlockDef::name);
    again->nodes = std::move(body.nodes);
    again->edges = std::move(body.edges);
    again->probes = std::move(body.probes);
    return commit(std::move(next));
}

void PipelineModel::remember() {
    undo_.push_back(snapshot());
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
}

void PipelineModel::restore(Snapshot s) {
    doc_ = std::move(s.doc);
    path_ = std::move(s.path);
    saved_ = std::move(s.saved);
    dirty_ = true;
    rebuild();
}

core::Result<void> PipelineModel::commit(pipeline::PipelineDoc next) {
    if (auto check = pipeline::build_graph(next, base_dir(), {.partial = true}); !check) return core::make_error(core::Errc::invalid_argument, check.error().message);
    remember();
    doc_ = std::move(next);
    dirty_ = true;
    rebuild();
    return {};
}

std::optional<std::string> PipelineModel::take_migration_notice() {
    if (!migration_pending_) return std::nullopt;
    migration_pending_ = false;
    return fmt::format(fmt::runtime(tr("pipeline.migrate.notice")), fmt::arg("from", migrated_from_), fmt::arg("to", pipeline::kSigVersion),
                       fmt::arg("n", migration_notes_.size()));
}

void PipelineModel::accept(pipeline::Loaded loaded) {
    remember();
    inside_.clear();
    marked.clear();
    migrated_from_ = loaded.from_version;
    migration_notes_ = std::move(loaded.notes);
    migration_pending_ = migrated_from_ < pipeline::kSigVersion;
    doc_ = std::move(loaded.doc);
}

core::Result<void> PipelineModel::load(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) return core::make_error(core::Errc::io_error, fmt::format(fmt::runtime(tr("pipeline.cli.cannot_read")), fmt::arg("path", file.string())));
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    auto loaded = pipeline::load_sig(text, {std::filesystem::absolute(file).parent_path()});
    if (!loaded) return std::unexpected{sig_failure(file, loaded.error())};
    accept(std::move(*loaded));
    saved_ = doc_;
    path_ = file;
    dirty_ = migration_pending_;
    selection = {};
    rebuild();
    return {};
}

core::Result<void> PipelineModel::load_text(std::string_view name, std::string_view text) {
    auto loaded = pipeline::load_sig(text, {base_dir()});
    if (!loaded) return std::unexpected{sig_failure(std::filesystem::path{std::string{name} + ".sig"}, loaded.error())};
    accept(std::move(*loaded));
    saved_ = {};
    path_.reset();
    dirty_ = true;
    selection = {};
    rebuild();
    return {};
}

core::Result<void> PipelineModel::save(const std::filesystem::path& file) {
    std::error_code ec;
    const auto from = std::filesystem::weakly_canonical(std::filesystem::absolute(base_dir()), ec);
    const auto to = std::filesystem::weakly_canonical(std::filesystem::absolute(file).parent_path(), ec);
    auto next = doc_;
    if (from != to)
        for (auto& n : next.nodes) {
            const auto* type = pipeline::find_block_type(n.type);
            if (!type) continue;
            for (const auto& spec : type->params) {
                if (spec.kind != pipeline::ParamSpec::Kind::path) continue;
                const auto it = n.params.find(spec.name);
                if (it == n.params.end()) continue;
                const auto value = pipeline::utf8_path(it->second);
                if (value.is_absolute()) continue;
                const auto target = std::filesystem::weakly_canonical(from / value, ec);
                auto moved = target.lexically_relative(to);
                if (moved.empty()) moved = target;
                const auto text = moved.generic_u8string();
                it->second.assign(reinterpret_cast<const char*>(text.data()), text.size());
            }
        }
    std::filesystem::create_directories(std::filesystem::absolute(file).parent_path(), ec);
    {
        std::ofstream out{file, std::ios::binary};
        out << pipeline::write_sig(next);
        if (!out) return core::make_error(core::Errc::io_error, fmt::format(fmt::runtime(tr("tui.pipeline.cannot_write")), fmt::arg("path", file.string())));
    }
    doc_ = std::move(next);
    path_ = file;
    saved_ = doc_;
    dirty_ = false;
    rebuild();
    return {};
}

std::string PipelineModel::add_node(std::string_view type) {
    std::string id;
    const auto* def = inside_block();
    const auto taken = [&](const pipeline::PipelineDoc& body, const std::string& name) {
        if (std::ranges::any_of(body.nodes, [&](const auto& n) { return n.id == name; })) return true;
        if (!def) return pipeline::find_own_block(body, name) != nullptr;
        return std::ranges::any_of(def->ins, [&](const auto& p) { return p.name == name; }) || std::ranges::any_of(def->outs, [&](const auto& p) { return p.name == name; });
    };
    const auto done = change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc&) -> core::Result<void> {
        id = std::string{type};
        for (int k = 2; taken(body, id); ++k) id = std::string{type} + std::to_string(k);
        body.nodes.push_back({id, std::string{type}, {}, 0, false});
        return {};
    });
    return done ? id : std::string{};
}

std::string PipelineModel::duplicate_node(std::string_view source) {
    std::string id;
    const auto done = change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc&) -> core::Result<void> {
        const auto it = std::find_if(body.nodes.begin(), body.nodes.end(), [&](const auto& n) { return n.id == source; });
        if (it == body.nodes.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
        id = it->type;
        for (int k = 2; std::any_of(body.nodes.begin(), body.nodes.end(), [&](const auto& n) { return n.id == id; }) || pipeline::find_own_block(body, id); ++k)
            id = it->type + std::to_string(k);
        auto copy = *it;
        copy.id = id;
        copy.line = 0;
        copy.comments = {false, {}, it->comments.after};
        body.nodes.insert(it + 1, std::move(copy));
        return {};
    });
    if (!done) return {};
    selection = {Selection::Kind::node, id, {}};
    return id;
}

core::Result<void> PipelineModel::insert_into_wire(const pipeline::Edge& wire, std::string_view type) {
    std::string id;
    auto done = change([&](pipeline::PipelineDoc& next, pipeline::PipelineDoc&) -> core::Result<void> {
    id = std::string{type};
    for (int k = 2; std::any_of(next.nodes.begin(), next.nodes.end(), [&](const auto& n) { return n.id == id; }) || pipeline::find_own_block(next, id); ++k)
        id = std::string{type} + std::to_string(k);
    const auto it = std::find_if(next.edges.begin(), next.edges.end(), [&](const auto& e) { return same_wire(e, wire); });
    if (it == next.edges.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_wire"));
    const auto position = it - next.edges.begin();
    auto comments = std::move(it->comments);
    next.edges.erase(it);
    next.nodes.push_back({id, std::string{type}, {}, 0, false});
    const pipeline::PortRef middle{id, {}, 0, 0};
    pipeline::Edge in_edge, out_edge;
    in_edge.from = wire.from;
    in_edge.to = middle;
    in_edge.comments = std::move(comments);
    out_edge.from = middle;
    out_edge.to = wire.to;
    next.edges.insert(next.edges.begin() + position, in_edge);
    next.edges.insert(next.edges.begin() + position + 1, out_edge);
    return {};
    });
    if (!done) return done;
    selection = {Selection::Kind::node, id, {}};
    return {};
}

core::Result<void> PipelineModel::connect(const pipeline::PortRef& from, const pipeline::PortRef& to) {
    return change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc&) -> core::Result<void> {
        body.edges.push_back({from, to});
        return {};
    });
}

void PipelineModel::disconnect(const pipeline::Edge& wire) {
    (void)change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc&) -> core::Result<void> {
        pipeline::remove_edges(body, [&](const auto& e) { return same_wire(e, wire); });
        return {};
    });
}

void PipelineModel::remove_node(std::string_view id) {
    const auto* def = inside_block();
    if (def && !node_in(def->nodes, id)) return;
    const auto paths = def ? instances_of(doc_, def->name) : std::vector<std::string>{};
    (void)change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc& doc) -> core::Result<void> {
        pipeline::remove_node(body, id);
        for (const auto& at : paths)
            std::erase_if(doc.probes, [&](const pipeline::ProbeDecl& p) { return p.at.node == at + "/" + std::string{id} || p.at.node.starts_with(at + "/" + std::string{id} + "/"); });
        return {};
    });
    if (selection.node == id || selection.wire.from.node == id || selection.wire.to.node == id) selection = {};
    marked.erase(std::string{id});
}

core::Result<void> PipelineModel::set_param(std::string_view node, std::string_view param, std::string_view value) {
    return change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc&) {
        return pipeline::apply_set(body, std::string{node} + "." + std::string{param} + "=" + std::string{value});
    });
}

core::Result<void> PipelineModel::set_variable(std::string_view name, std::string_view value) {
    auto next = doc_;
    if (auto ok = pipeline::apply_set(next, std::string{name} + "=" + std::string{value}); !ok) return ok;
    return commit(std::move(next));
}

core::Result<void> PipelineModel::add_variable(std::string_view name, std::string_view value) {
    const auto valid = !name.empty() && (std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_') &&
                       std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    if (!valid || value.empty()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.vars.bad_add"));
    if (std::any_of(doc_.lets.begin(), doc_.lets.end(), [&](const auto& l) { return l.name == name; }))
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.pipeline.vars.exists")), fmt::arg("name", name)));
    auto next = doc_;
    next.version = pipeline::kSigVersion;
    next.lets.push_back({std::string{name}, std::string{value}});
    return commit(std::move(next));
}

core::Result<void> PipelineModel::remove_variable(std::string_view name) {
    if (const auto users = uses_of(name); !users.empty()) {
        std::string list;
        for (const auto& u : users) list += (list.empty() ? "" : ", ") + u;
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.pipeline.vars.used")), fmt::arg("name", name), fmt::arg("nodes", list)));
    }
    auto next = doc_;
    std::erase_if(next.lets, [&](const auto& l) { return l.name == name; });
    return commit(std::move(next));
}

std::vector<std::string> PipelineModel::uses_of(std::string_view name) const {
    const auto mentions = [&](const std::string& value) {
        const auto names = pipeline::variables_in(value);
        return std::ranges::find(names, name) != names.end();
    };
    std::vector<std::string> out;
    for (const auto& n : doc_.nodes)
        if (std::any_of(n.params.begin(), n.params.end(), [&](const auto& p) { return mentions(p.second); })) out.push_back(n.id);
    for (const auto& l : doc_.lets)
        if (l.name != name && mentions(l.value)) out.push_back("$" + l.name);
    return out;
}

core::Result<void> PipelineModel::toggle_bypass(std::string_view node) {
    return change([&](pipeline::PipelineDoc& next, pipeline::PipelineDoc&) -> core::Result<void> {
        const auto it = std::find_if(next.nodes.begin(), next.nodes.end(), [&](const auto& n) { return n.id == node; });
        if (it == next.nodes.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
        it->bypass = !it->bypass;
        return {};
    });
}

void PipelineModel::toggle_probe(const pipeline::PortRef& local) {
    auto at = local;
    at.node = scoped(local.node);
    auto next = doc_;
    const auto before = next.probes.size();
    std::erase_if(next.probes, [&](const auto& p) { return same_port(p.at, at); });
    if (next.probes.size() == before) next.probes.push_back({at, {}});
    (void)commit(std::move(next));
}

core::Result<void> PipelineModel::rename_probe(const pipeline::PortRef& local, std::string name) {
    auto at = local;
    at.node = scoped(local.node);
    auto next = doc_;
    const auto decl = std::find_if(next.probes.begin(), next.probes.end(), [&](const auto& p) { return same_port(p.at, at); });
    if (decl == next.probes.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_probe"));
    const auto resolved = [&]() -> std::string {
        if (graph_)
            for (const auto& p : graph_->probes)
                if (same_port(p.at, at)) return p.name;
        return {};
    };
    const auto before = resolved();
    decl->name = std::move(name);
    if (auto r = commit(std::move(next)); !r) return r;
    const auto after = resolved();
    std::lock_guard lock{run_->mutex};
    auto& probes = run_->view.probes;
    if (const auto it = probes.find(before); it != probes.end() && before != after) {
        auto value = it->second;
        probes.erase(it);
        probes.insert_or_assign(after, std::move(value));
    }
    return {};
}

core::Result<std::string> PipelineModel::collapse(std::string name) {
    const auto fail = [](const char* key, auto&&... args) -> core::Result<std::string> {
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr(key)), std::forward<decltype(args)>(args)...));
    };
    if (marked.empty()) return fail("tui.pipeline.block.nothing_marked");
    if (!identifier(name)) return fail("tui.pipeline.block.bad_name", fmt::arg("name", name));
    const bool taken = pipeline::find_block_type(name) || pipeline::find_own_block(doc_, name) || node_in(doc_.nodes, name) ||
                       std::ranges::any_of(doc_.lets, [&](const pipeline::LetDecl& l) { return l.name == name; });
    if (taken) return fail("tui.pipeline.block.name_taken", fmt::arg("name", name));
    const auto* scope_def = inside_block();
    const auto prefixes = scope_def ? instances_of(doc_, scope_def->name) : std::vector<std::string>{std::string{}};
    std::string call;
    auto done = change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc& doc) -> core::Result<void> {
        for (const auto& id : marked)
            if (!node_in(body.nodes, id)) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
        pipeline::BlockDef def;
        def.name = name;
        const auto values = pipeline::let_values(body.lets);
        std::set<std::string, std::less<>> used;
        for (const auto& n : body.nodes)
            if (marked.contains(n.id))
                for (const auto& [key, value] : n.params)
                    for (auto& v : pipeline::variables_in(value)) used.insert(std::move(v));
        for (const auto& v : used) {
            const auto it = std::ranges::find(values, v, &pipeline::LetValue::name);
            def.order.push_back(v);
            def.defaults[v] = it == values.end() ? std::string{} : it->expanded;
        }
        std::set<std::string, std::less<>> names{marked.begin(), marked.end()};
        const auto port_name = [&](std::string wanted, const std::string& node, const char* plain) {
            if (wanted == "in" || wanted == "out") wanted = plain;
            auto out = wanted.empty() || names.contains(wanted) ? node + "_" + wanted : wanted;
            if (wanted.empty()) out = node;
            for (int k = 2; names.contains(out); ++k) out = node + "_" + wanted + std::to_string(k);
            names.insert(out);
            return out;
        };
        const auto input_name = [&](const pipeline::PortRef& to) {
            if (!to.port.empty()) return to.port;
            const auto* n = node_in(body.nodes, to.node);
            if (const auto* t = n ? pipeline::find_block_type(n->type) : nullptr) {
                if (t->inputs.size() == 1) return t->inputs.front().name;
                for (const auto& in : t->inputs)
                    if (!in.optional) return in.name;
            }
            if (const auto* d = n ? pipeline::find_own_block(body, n->type) : nullptr; d && !d->ins.empty()) return d->ins.front().name;
            return std::string{"x"};
        };
        const auto output_name = [&](const pipeline::PortRef& from) {
            if (!from.port.empty()) return from.port;
            const auto* n = node_in(body.nodes, from.node);
            if (const auto* t = n ? pipeline::find_block_type(n->type) : nullptr; t && !t->outputs.empty()) return t->outputs.front().name;
            if (const auto* d = n ? pipeline::find_own_block(body, n->type) : nullptr; d && !d->outs.empty()) return d->outs.front().name;
            return std::string{"y"};
        };
        call = name + "1";
        for (int k = 2; node_in(body.nodes, call) || pipeline::find_own_block(doc, call); ++k) call = name + std::to_string(k);
        std::map<std::string, std::string, std::less<>> ins, outs;
        std::vector<pipeline::Edge> kept;
        for (const auto& e : body.edges) {
            const bool from_in = marked.contains(e.from.node), to_in = marked.contains(e.to.node);
            if (from_in && to_in) {
                def.edges.push_back(e);
            } else if (to_in) {
                const auto key = ref_text(e.from);
                auto it = ins.find(key);
                if (it == ins.end()) {
                    it = ins.emplace(key, port_name(input_name(e.to), e.to.node, "x")).first;
                    def.ins.push_back({it->second, false, 0});
                    kept.push_back({e.from, {call, it->second, 0, 0}});
                }
                def.edges.push_back({{it->second, {}, 0, 0}, e.to});
            } else if (from_in) {
                const auto key = ref_text(e.from);
                auto it = outs.find(key);
                if (it == outs.end()) {
                    it = outs.emplace(key, port_name(output_name(e.from), e.from.node, "y")).first;
                    def.outs.push_back({it->second, false, 0});
                    def.edges.push_back({e.from, {it->second, {}, 0, 0}});
                }
                kept.push_back({{call, it->second, 0, 0}, e.to});
            } else {
                kept.push_back(e);
            }
        }
        for (std::size_t k = 0; k < def.ins.size(); ++k) def.ins[k].line = static_cast<int>(k);
        for (std::size_t k = 0; k < def.outs.size(); ++k) def.outs[k].line = static_cast<int>(def.ins.size() + k);
        pipeline::NodeDecl node{call, name, {}, 0, false};
        for (const auto& v : used) node.params[v] = "$" + v;
        std::vector<pipeline::NodeDecl> nodes;
        bool placed = false;
        for (auto& n : body.nodes) {
            if (!marked.contains(n.id)) {
                nodes.push_back(std::move(n));
                continue;
            }
            if (!placed) nodes.push_back(node), placed = true;
            n.line = 0;
            n.comments = {};
            def.nodes.push_back(std::move(n));
        }
        body.nodes = std::move(nodes);
        body.edges = std::move(kept);
        for (auto& probe : body.probes)
            if (marked.contains(probe.at.node)) {
                if (probe.name.empty()) probe.name = ref_text(probe.at);
                probe.at.node = call + "/" + probe.at.node;
            }
        for (const auto& prefix : prefixes) {
            const auto scope = prefix.empty() ? std::string{} : prefix + "/";
            for (auto& probe : doc.probes) {
                if (!probe.at.node.starts_with(scope)) continue;
                const auto rest = probe.at.node.substr(scope.size());
                if (!marked.contains(rest.substr(0, rest.find('/')))) continue;
                if (probe.name.empty()) probe.name = ref_text(probe.at);
                probe.at.node = scope + call + "/" + rest;
            }
        }
        doc.blocks.push_back(std::move(def));
        return {};
    });
    if (!done) return std::unexpected{done.error()};
    marked.clear();
    selection = {Selection::Kind::node, call, {}};
    return call;
}

core::Result<void> PipelineModel::expand(std::string_view call) {
    const auto* scope_def = inside_block();
    const auto prefixes = scope_def ? instances_of(doc_, scope_def->name) : std::vector<std::string>{std::string{}};
    std::string first;
    auto done = change([&](pipeline::PipelineDoc& body, pipeline::PipelineDoc& doc) -> core::Result<void> {
        const auto at = std::ranges::find(body.nodes, call, &pipeline::NodeDecl::id);
        if (at == body.nodes.end()) return core::make_error(core::Errc::invalid_argument, tr("tui.pipeline.no_node"));
        const auto* found = pipeline::find_own_block(body, at->type);
        if (!found) return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.pipeline.block.not_call")), fmt::arg("node", call)));
        const auto def = *found;
        const auto c = *at;
        std::map<std::string, std::string, std::less<>> values;
        for (const auto& key : def.order) values[key] = c.params.contains(key) ? c.params.at(key) : def.defaults.at(key);
        for (const auto& l : def.lets) values[l.name] = l.value;
        std::map<std::string, std::string, std::less<>> renamed;
        for (const auto& n : def.nodes) {
            auto id = n.id;
            for (int k = 2; (id != c.id && node_in(body.nodes, id)) || std::ranges::any_of(renamed, [&](const auto& r) { return r.second == id; }) || pipeline::find_own_block(doc, id);
                 ++k)
                id = c.id + "_" + n.id + (k > 2 ? std::to_string(k - 1) : std::string{});
            renamed[n.id] = id;
        }
        std::vector<pipeline::NodeDecl> inner;
        for (const auto& n : def.nodes) {
            auto copy = n;
            copy.id = renamed.at(n.id);
            copy.line = 0;
            copy.comments = {};
            for (auto& [key, value] : copy.params) value = bind_params(value, values, kind_of(n, key));
            inner.push_back(std::move(copy));
        }
        if (!inner.empty()) first = inner.front().id;
        const auto is_in = [&](const std::string& n) { return std::ranges::any_of(def.ins, [&](const auto& p) { return p.name == n; }); };
        const auto is_out = [&](const std::string& n) { return std::ranges::any_of(def.outs, [&](const auto& p) { return p.name == n; }); };
        const auto source_of = [&](const std::string& port) -> std::optional<pipeline::PortRef> {
            for (const auto& e : body.edges)
                if (e.to.node == c.id && (e.to.port == port || (e.to.port.empty() && def.ins.size() == 1))) return e.from;
            return std::nullopt;
        };
        const auto targets_of = [&](const std::string& port) {
            std::vector<pipeline::PortRef> out;
            for (const auto& e : body.edges)
                if (e.from.node == c.id && (e.from.port == port || (e.from.port.empty() && def.outs.size() == 1))) out.push_back(e.to);
            return out;
        };
        std::vector<pipeline::Edge> edges;
        for (const auto& e : body.edges)
            if (e.from.node != c.id && e.to.node != c.id) edges.push_back(e);
        for (const auto& e : def.edges) {
            std::optional<pipeline::PortRef> from = e.from;
            if (is_in(e.from.node)) from = source_of(e.from.node);
            else from->node = renamed.at(e.from.node);
            if (!from) continue;
            if (is_out(e.to.node)) {
                for (const auto& t : targets_of(e.to.node)) edges.push_back({*from, t});
            } else {
                auto to = e.to;
                to.node = renamed.at(e.to.node);
                edges.push_back({*from, to});
            }
        }
        const auto position = at - body.nodes.begin();
        body.nodes.erase(at);
        body.nodes.insert(body.nodes.begin() + position, inner.begin(), inner.end());
        body.edges = std::move(edges);
        for (const auto& probe : def.probes) {
            auto moved = probe;
            moved.name = c.id + "/" + (probe.name.empty() ? ref_text(probe.at) : probe.name);
            moved.at.node = renamed.at(probe.at.node);
            body.probes.push_back(std::move(moved));
        }
        for (const auto& prefix : prefixes) {
            const auto scope = (prefix.empty() ? std::string{} : prefix + "/") + c.id + "/";
            for (auto& probe : doc.probes) {
                if (!probe.at.node.starts_with(scope)) continue;
                auto rest = probe.at.node.substr(scope.size());
                const auto head = rest.substr(0, rest.find('/'));
                if (!renamed.contains(head)) continue;
                if (probe.name.empty()) probe.name = ref_text(probe.at);
                probe.at.node = scope.substr(0, scope.size() - c.id.size() - 1) + renamed.at(head) + rest.substr(head.size());
            }
        }
        return {};
    });
    if (!done) return done;
    selection = first.empty() ? Selection{} : Selection{Selection::Kind::node, first, {}};
    return {};
}

core::Result<void> PipelineModel::enter(std::string_view call) {
    const auto* def = inside_block();
    const auto& nodes = def ? def->nodes : doc_.nodes;
    const auto* n = node_in(nodes, call);
    if (!n || !pipeline::find_own_block(doc_, n->type))
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.pipeline.block.not_call")), fmt::arg("node", call)));
    inside_ = scoped(call);
    selection = {};
    marked.clear();
    rebuild();
    return {};
}

bool PipelineModel::leave() {
    if (inside_.empty()) return false;
    const auto slash = inside_.rfind('/');
    const auto from = slash == std::string::npos ? inside_ : inside_.substr(slash + 1);
    inside_ = slash == std::string::npos ? std::string{} : inside_.substr(0, slash);
    selection = {Selection::Kind::node, from, {}};
    marked.clear();
    rebuild();
    return true;
}

bool PipelineModel::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(snapshot());
    auto s = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(s));
    return true;
}

bool PipelineModel::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(snapshot());
    auto s = std::move(redo_.back());
    redo_.pop_back();
    restore(std::move(s));
    return true;
}

void PipelineModel::run(JobRunner& jobs) {
    if (problem_ || !graph_) {
        std::lock_guard lock{run_->mutex};
        run_->view.error = problem_ ? problem_->message : tr("tui.pipeline.empty");
        return;
    }
    bool busy = false;
    {
        std::lock_guard lock{run_->mutex};
        busy = run_->job.has_value();
    }
    if (busy) cancel(jobs);
    auto copy = *graph_;
    std::optional<std::size_t> source;
    for (std::size_t i = 0; i < copy.nodes.size() && !source; ++i)
        if (copy.nodes[i].type->id == "wav") source = i;
    for (std::size_t i = 0; i < copy.nodes.size() && !source; ++i)
        if (copy.nodes[i].type->inputs.empty() && !copy.nodes[i].type->outputs.empty() && copy.nodes[i].type->outputs.front().type == pipeline::PortType::signal)
            source = i;
    if (source) copy.probes.push_back({std::string{kInputProbe}, {copy.nodes[*source].id, {}, 0, 0}, {*source, 0, {}}});
    auto graph = std::make_shared<const pipeline::Graph>(std::move(copy));
    auto token = std::make_shared<core::CancelToken>();
    std::uint64_t number = 0;
    {
        std::lock_guard lock{run_->mutex};
        auto& v = run_->view;
        number = ++v.number;
        v.running = true;
        v.cancelled = false;
        v.error.reset();
        v.nodes.clear();
        v.probes.clear();
        v.reports.clear();
        v.input.reset();
        for (const auto& n : graph->nodes) v.nodes[n.id].status.id = n.id;
        run_->cancel = token;
    }
    auto state = run_;
    auto cache = cache_;
    auto listener = run_listener;
    auto finish = finish_listener;
    FinishedRun finished{number, path_, finish ? pipeline::write_sig(doc_) : std::string{}, false};
    const auto id = jobs.submit(tr("tui.pipeline.job"), {tr("tui.pipeline.job")}, [state, cache, graph, token, listener, number, finish,
                                                                                    finished](JobContext& ctx) mutable {
        ctx.begin(0);
        const auto started = std::chrono::steady_clock::now();
        const pipeline::RunEvents events{[&](const pipeline::NodeStatus& s) {
            double done = 0;
            {
                std::lock_guard lock{state->mutex};
                if (state->view.number != number) return;
                state->view.nodes[s.id].status = s;
                for (const auto& [name, n] : state->view.nodes) done += n.status.fraction;
                done /= static_cast<double>(std::max<std::size_t>(1, state->view.nodes.size()));
            }
            ctx.report(done);
            if (listener) listener(s);
        }};
        auto result = pipeline::run_graph(*graph, *cache, events, *token);
        finished.ok = result.has_value();
        {
            std::lock_guard lock{state->mutex};
            auto& v = state->view;
            if (v.number != number) {
                ctx.finish();
                return;
            }
            v.running = false;
            v.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            state->job.reset();
            if (!result) {
                if (result.error().code == core::Errc::cancelled) v.cancelled = true;
                else v.error = result.error().message;
            } else {
                for (const auto& s : result->nodes) v.nodes[s.id].status = s;
                for (auto& [name, value] : result->probes) {
                    auto shared = std::make_shared<const pipeline::Value>(std::move(value));
                    if (name == kInputProbe) v.input = std::move(shared);
                    else v.probes.emplace(name, std::move(shared));
                }
                v.reports = std::move(result->reports);
                if (v.seconds > 0.05) {
                    std::string slowest;
                    double longest = -1;
                    for (const auto& [name, n] : v.nodes)
                        if (n.status.state == pipeline::NodeState::done && n.status.wall_seconds > longest) {
                            longest = n.status.wall_seconds;
                            slowest = name;
                        }
                    if (!slowest.empty()) v.nodes[slowest].bottleneck = true;
                }
            }
        }
        ctx.finish();
        if (finish) finish(finished);
    });
    std::lock_guard lock{run_->mutex};
    if (run_->view.running && run_->view.number == number) run_->job = id;
}

void PipelineModel::cancel(JobRunner& jobs) {
    std::optional<std::uint64_t> job;
    {
        std::lock_guard lock{run_->mutex};
        if (run_->cancel) run_->cancel->cancel();
        job = run_->job;
    }
    if (job) jobs.cancel(*job);
    std::lock_guard lock{sweep_->mutex};
    if (sweep_->cancel && sweep_->view.running) sweep_->cancel->cancel();
}

void PipelineModel::sweep(JobRunner& jobs, pipeline::SweepAxis axis, std::vector<pipeline::Measure> measures) {
    auto token = std::make_shared<core::CancelToken>();
    std::uint64_t number = 0;
    {
        std::lock_guard lock{sweep_->mutex};
        if (sweep_->cancel) sweep_->cancel->cancel();
        sweep_->cancel = token;
        number = ++sweep_->view.number;
        sweep_->view = SweepView{number, true, 0, axis.values.size(), std::nullopt, std::nullopt};
    }
    auto state = sweep_;
    auto doc = doc_;
    auto dir = base_dir();
    jobs.submit(tr("tui.pipeline.sweep.job"), {tr("tui.pipeline.sweep.job")}, [state, doc = std::move(doc), dir = std::move(dir), axis = std::move(axis),
                                                                                 measures = std::move(measures), token, number](JobContext& ctx) mutable {
        ctx.begin(0);
        auto result = pipeline::run_sweep(doc, dir, std::move(axis), std::move(measures), *token, [&](std::size_t done, std::size_t total) {
            ctx.report(total ? static_cast<double>(done) / static_cast<double>(total) : 1.0);
            std::lock_guard lock{state->mutex};
            if (state->view.number == number) state->view.done = done;
        });
        std::lock_guard lock{state->mutex};
        if (state->view.number == number) {
            state->view.running = false;
            if (result) state->view.result = std::move(*result);
            else state->view.error = result.error().message;
        }
        ctx.finish();
    });
}

SweepView PipelineModel::sweep_view() const {
    std::lock_guard lock{sweep_->mutex};
    return sweep_->view;
}

RunView PipelineModel::run_view() const {
    RunView v;
    {
        std::lock_guard lock{run_->mutex};
        v = run_->view;
    }
    if (!doc_.blocks.empty() || !doc_.imported.empty()) {
        const auto prefix = inside_.empty() ? std::string{} : inside_ + "/";
        std::map<std::string, NodeRun, std::less<>> shown;
        std::map<std::string, std::vector<const NodeRun*>, std::less<>> groups;
        for (const auto& [id, n] : v.nodes) {
            if (!id.starts_with(prefix)) continue;
            const auto rest = id.substr(prefix.size());
            const auto slash = rest.find('/');
            if (slash == std::string::npos) shown[rest] = n;
            else groups[rest.substr(0, slash)].push_back(&n);
        }
        for (const auto& [id, members] : groups) {
            NodeRun sum;
            sum.status.id = id;
            const auto all = [&](pipeline::NodeState s) { return std::ranges::all_of(members, [&](const NodeRun* r) { return r->status.state == s; }); };
            const auto any = [&](pipeline::NodeState s) { return std::ranges::any_of(members, [&](const NodeRun* r) { return r->status.state == s; }); };
            if (any(pipeline::NodeState::failed)) sum.status.state = pipeline::NodeState::failed;
            else if (any(pipeline::NodeState::running) || (any(pipeline::NodeState::waiting) && !all(pipeline::NodeState::waiting))) sum.status.state = pipeline::NodeState::running;
            else if (all(pipeline::NodeState::waiting)) sum.status.state = pipeline::NodeState::waiting;
            else if (all(pipeline::NodeState::cached)) sum.status.state = pipeline::NodeState::cached;
            else sum.status.state = pipeline::NodeState::done;
            for (const auto* r : members) {
                sum.status.wall_seconds += r->status.wall_seconds;
                sum.status.cpu_seconds += r->status.cpu_seconds;
                sum.status.bytes += r->status.bytes;
                sum.status.fraction += r->status.fraction / static_cast<double>(members.size());
                sum.bottleneck = sum.bottleneck || r->bottleneck;
                if (sum.status.message.empty()) sum.status.message = r->status.message;
            }
            shown[id] = std::move(sum);
        }
        v.nodes = std::move(shown);
    }
    const auto& visible = inside_.empty() ? doc_.nodes : view_doc_.nodes;
    std::erase_if(v.nodes, [&](const auto& entry) {
        return std::none_of(visible.begin(), visible.end(), [&](const auto& n) { return n.id == entry.first; });
    });
    const auto shape = [](const pipeline::PipelineDoc& d) {
        std::string out;
        for (const auto& n : d.nodes) out += n.id + ":" + n.type + (n.bypass ? "!" : "") + ";";
        for (const auto& e : d.edges) out += e.from.node + "." + e.from.port + ">" + e.to.node + "." + e.to.port + ";";
        for (const auto& p : d.probes) out += p.at.node + "." + p.at.port + "@" + p.name + ";";
        return out;
    };
    v.structure_changed = shape(doc_) != shape(saved_);
    const auto quoted = [](const std::string& text) {
        if (std::all_of(text.begin(), text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || std::string_view{"._-=/,:+"}.find(c) != std::string_view::npos; }))
            return text;
        std::string out = "'";
        for (const char c : text) out += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
        return out + "'";
    };
    for (const auto& n : doc_.nodes) {
        const auto before = std::find_if(saved_.nodes.begin(), saved_.nodes.end(), [&](const auto& s) { return s.id == n.id; });
        if (before == saved_.nodes.end()) continue;
        for (const auto& [key, value] : n.params) {
            const auto old = before->params.find(key);
            if (old == before->params.end() || old->second != value) v.sets.push_back(quoted(n.id + "." + key + "=" + value));
        }
    }
    return v;
}

std::size_t PipelineModel::cache_bytes() const { return cache_->bytes(); }

std::string shape_text(const std::vector<pipeline::OutputShape>& shapes, bool compact) {
    std::string out;
    for (const auto& s : shapes) {
        const bool sized = s.type == pipeline::PortType::signal || s.type == pipeline::PortType::coeffs;
        if (compact && !sized) continue;
        std::string one;
        if (compact) one = std::to_string(s.channels) + "×" + std::to_string(s.frames);
        else if (sized) {
            one = std::to_string(s.channels) + " " + tr("tui.pipeline.channels") + " × " + std::to_string(s.frames);
            if (s.type == pipeline::PortType::coeffs) one = tr("tui.pipeline.shape.coeffs") + " " + one;
            if (s.rate > 0) one += " · " + format_si(s.rate, tr("unit.hz"));
        } else {
            one = std::string{pipeline::to_string(s.type)};
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return out;
}

}  // namespace signals::tui
