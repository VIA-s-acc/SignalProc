#include "tui/screens/pipeline_model.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "tui/widgets/format.hpp"
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

core::Result<void> PipelineModel::load(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) return core::make_error(core::Errc::io_error, fmt::format(fmt::runtime(tr("pipeline.cli.cannot_read")), fmt::arg("path", file.string())));
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    auto doc = pipeline::parse_sig(text);
    if (!doc) return std::unexpected{sig_failure(file, doc.error())};
    remember();
    doc_ = std::move(*doc);
    saved_ = doc_;
    path_ = file;
    dirty_ = false;
    selection = {};
    rebuild();
    return {};
}

core::Result<void> PipelineModel::load_text(std::string_view name, std::string_view text) {
    auto doc = pipeline::parse_sig(text);
    if (!doc) return std::unexpected{sig_failure(std::filesystem::path{std::string{name} + ".sig"}, doc.error())};
    remember();
    doc_ = std::move(*doc);
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
    std::string id{type};
    for (int k = 2; std::any_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == id; }); ++k) id = std::string{type} + std::to_string(k);
    auto next = doc_;
    next.nodes.push_back({id, std::string{type}, {}, 0, false});
    if (!commit(std::move(next))) return {};
    return id;
}

std::string PipelineModel::duplicate_node(std::string_view source) {
    const auto it = std::find_if(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == source; });
    if (it == doc_.nodes.end()) return {};
    std::string id = it->type;
    for (int k = 2; std::any_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == id; }); ++k) id = it->type + std::to_string(k);
    auto next = doc_;
    auto copy = *it;
    copy.id = id;
    copy.line = 0;
    copy.comments = {false, {}, it->comments.after};
    next.nodes.insert(next.nodes.begin() + (it - doc_.nodes.begin()) + 1, std::move(copy));
    if (!commit(std::move(next))) return {};
    selection = {Selection::Kind::node, id, {}};
    return id;
}

core::Result<void> PipelineModel::insert_into_wire(const pipeline::Edge& wire, std::string_view type) {
    std::string id{type};
    for (int k = 2; std::any_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == id; }); ++k) id = std::string{type} + std::to_string(k);
    auto next = doc_;
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
    pipeline::remove_edges(next, [&](const auto& e) { return same_wire(e, wire); });
    (void)commit(std::move(next));
}

void PipelineModel::remove_node(std::string_view id) {
    auto next = doc_;
    pipeline::remove_node(next, id);
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

core::Result<void> PipelineModel::rename_probe(const pipeline::PortRef& at, std::string name) {
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
    const auto id = jobs.submit(tr("tui.pipeline.job"), {tr("tui.pipeline.job")}, [state, cache, graph, token, listener, number](JobContext& ctx) {
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
            ctx.finish();
            return;
        }
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
        ctx.finish();
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
    std::erase_if(v.nodes, [&](const auto& entry) {
        return std::none_of(doc_.nodes.begin(), doc_.nodes.end(), [&](const auto& n) { return n.id == entry.first; });
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
