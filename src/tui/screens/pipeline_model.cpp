#include "tui/screens/pipeline_model.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "tui/monitor/sampler.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kUndoLimit = 100;
constexpr std::string_view kInputProbe = "@input";

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
    saved_ = doc_;
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
    saved_ = doc_;
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

void PipelineModel::run(JobRunner& jobs) {
    if (problem_ || !graph_) {
        std::lock_guard lock{run_->mutex};
        run_->view.error = problem_ ? problem_->message : tr("tui.pipeline.empty");
        return;
    }
    if (run_->job) cancel(jobs);
    auto copy = *graph_;
    for (std::size_t i = 0; i < copy.nodes.size(); ++i)
        if (copy.nodes[i].type->inputs.empty() && !copy.nodes[i].type->outputs.empty() && copy.nodes[i].type->outputs.front().type == pipeline::PortType::signal) {
            copy.probes.push_back({std::string{kInputProbe}, {copy.nodes[i].id, {}, 0, 0}, {i, 0, {}}});
            break;
        }
    auto graph = std::make_shared<const pipeline::Graph>(std::move(copy));
    auto token = std::make_shared<core::CancelToken>();
    {
        std::lock_guard lock{run_->mutex};
        auto& v = run_->view;
        ++v.number;
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
    const auto id = jobs.submit(tr("tui.pipeline.job"), {tr("tui.pipeline.job")}, [state, cache, graph, token, listener](JobContext& ctx) {
        ctx.begin(0);
        const auto started = std::chrono::steady_clock::now();
        const pipeline::RunEvents events{[&](const pipeline::NodeStatus& s) {
            double done = 0;
            {
                std::lock_guard lock{state->mutex};
                state->view.nodes[s.id].status = s;
                for (const auto& [name, n] : state->view.nodes) done += n.status.fraction;
                done /= static_cast<double>(std::max<std::size_t>(1, state->view.nodes.size()));
            }
            ctx.report(done);
            if (listener) listener(s);
        }};
        auto result = pipeline::run_graph(*graph, *cache, events, *token);
        std::lock_guard lock{state->mutex};
        auto& v = state->view;
        v.running = false;
        v.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        state->job.reset();
        if (!result) {
            if (result.error().code == core::Errc::cancelled) v.cancelled = true;
            else v.error = result.error().message;
            ctx.finish();
            return;
        }
        for (const auto& s : result->nodes) v.nodes[s.id].status = s;
        v.probes = std::move(result->probes);
        if (const auto it = v.probes.find(kInputProbe); it != v.probes.end()) {
            v.input = std::move(it->second);
            v.probes.erase(it);
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
        ctx.finish();
    });
    std::lock_guard lock{run_->mutex};
    if (run_->view.running) run_->job = id;
}

void PipelineModel::cancel(JobRunner& jobs) {
    std::optional<std::uint64_t> job;
    {
        std::lock_guard lock{run_->mutex};
        if (run_->cancel) run_->cancel->cancel();
        job = run_->job;
    }
    if (job) jobs.cancel(*job);
}

RunView PipelineModel::run_view() const {
    RunView v;
    {
        std::lock_guard lock{run_->mutex};
        v = run_->view;
    }
    std::erase_if(v.nodes, [&](const auto& entry) {
        return std::none_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == entry.first; });
    });
    for (const auto& n : doc_.nodes) {
        const auto before = std::find_if(saved_.nodes.begin(), saved_.nodes.end(), [&](const auto& s) { return s.id == n.id; });
        if (before == saved_.nodes.end()) continue;
        for (const auto& [key, value] : n.params) {
            const auto old = before->params.find(key);
            if (old == before->params.end() || old->second != value) v.sets.push_back(n.id + "." + key + "=" + value);
        }
    }
    return v;
}

std::size_t PipelineModel::cache_bytes() const { return cache_->bytes(); }

}  // namespace signals::tui
