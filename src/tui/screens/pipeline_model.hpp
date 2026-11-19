#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/sig.hpp"
#include "pipeline/sweep.hpp"
#include "tui/jobs/jobs.hpp"

namespace signals::tui {

struct Selection {
    enum class Kind { none, node, wire } kind = Kind::none;
    std::string node;
    pipeline::Edge wire;
};

struct NodeRun {
    pipeline::NodeStatus status;
    bool bottleneck = false;
};

struct SweepView {
    std::uint64_t number = 0;
    bool running = false;
    std::size_t done = 0, total = 0;
    std::optional<pipeline::SweepResult> result;
    std::optional<std::string> error;
};

struct RunView {
    std::uint64_t number = 0;
    bool running = false, cancelled = false;
    std::optional<std::string> error;
    std::map<std::string, NodeRun, std::less<>> nodes;
    std::map<std::string, std::shared_ptr<const pipeline::Value>, std::less<>> probes;
    std::vector<pipeline::NodeReport> reports;
    std::vector<std::string> sets;
    double seconds = 0;
    std::shared_ptr<const pipeline::Value> input;
    bool structure_changed = false;
};

struct FinishedRun {
    std::uint64_t number = 0;
    std::optional<std::filesystem::path> path = {};
    std::string sig = {};
    bool ok = false;
};

class PipelineModel {
public:
    PipelineModel();

    const pipeline::PipelineDoc& doc() const { return doc_; }
    const std::optional<std::filesystem::path>& path() const { return path_; }
    bool dirty() const { return dirty_; }
    std::optional<pipeline::SigError> problem() const { return problem_; }
    const pipeline::Graph* graph() const { return view_ ? &*view_ : nullptr; }
    const pipeline::Graph* full_graph() const { return graph_ ? &*graph_ : nullptr; }
    const pipeline::PipelineDoc& view_doc() const { return inside_.empty() ? doc_ : view_doc_; }
    const std::string& inside() const { return inside_; }
    const pipeline::BlockDef* inside_block() const;
    std::size_t calls_of(std::string_view block) const;
    std::filesystem::path base_dir() const;
    std::size_t undo_depth() const { return undo_.size(); }
    int migrated_from() const { return migrated_from_; }
    const std::vector<std::string>& migration_notes() const { return migration_notes_; }
    std::optional<std::string> take_migration_notice();

    core::Result<void> load(const std::filesystem::path& file);
    core::Result<void> load_text(std::string_view name, std::string_view text);
    core::Result<void> save(const std::filesystem::path& file);
    std::string add_node(std::string_view type);
    std::string duplicate_node(std::string_view id);
    core::Result<void> insert_into_wire(const pipeline::Edge& wire, std::string_view type);
    core::Result<void> connect(const pipeline::PortRef& from, const pipeline::PortRef& to);
    void disconnect(const pipeline::Edge& wire);
    void remove_node(std::string_view id);
    core::Result<void> set_param(std::string_view node, std::string_view param, std::string_view value);
    core::Result<void> set_variable(std::string_view name, std::string_view value);
    core::Result<void> add_variable(std::string_view name, std::string_view value);
    core::Result<void> remove_variable(std::string_view name);
    std::vector<std::string> uses_of(std::string_view name) const;
    core::Result<void> toggle_bypass(std::string_view node);
    void toggle_probe(const pipeline::PortRef& at);
    core::Result<void> rename_probe(const pipeline::PortRef& at, std::string name);
    bool undo();
    bool redo();

    core::Result<std::string> collapse(std::string name);
    core::Result<void> expand(std::string_view call);
    core::Result<void> enter(std::string_view call);
    bool leave();

    void run(JobRunner& jobs);
    void cancel(JobRunner& jobs);
    RunView run_view() const;
    void sweep(JobRunner& jobs, pipeline::SweepAxis axis, std::vector<pipeline::Measure> measures);
    SweepView sweep_view() const;
    std::size_t cache_bytes() const;

    Selection selection;
    std::set<std::string, std::less<>> marked;
    std::function<void(const pipeline::NodeStatus&)> run_listener;
    std::function<void(const FinishedRun&)> finish_listener;

private:
    core::Result<void> commit(pipeline::PipelineDoc next);
    core::Result<void> change(const std::function<core::Result<void>(pipeline::PipelineDoc& body, pipeline::PipelineDoc& doc)>& edit);
    std::string scoped(std::string_view id) const { return inside_.empty() ? std::string{id} : inside_ + "/" + std::string{id}; }
    void rebuild();

    pipeline::PipelineDoc doc_;
    std::optional<std::filesystem::path> path_;
    bool dirty_ = false;
    struct Snapshot {
        pipeline::PipelineDoc doc;
        std::optional<std::filesystem::path> path;
        pipeline::PipelineDoc saved;
    };
    Snapshot snapshot() const { return {doc_, path_, saved_}; }
    void restore(Snapshot s);
    void remember();
    void accept(pipeline::Loaded loaded);
    std::vector<Snapshot> undo_, redo_;
    std::optional<pipeline::Graph> graph_, view_;
    pipeline::PipelineDoc view_doc_;
    std::string inside_;
    std::optional<pipeline::SigError> problem_;
    pipeline::PipelineDoc saved_;

    struct RunState {
        mutable std::mutex mutex;
        RunView view;
        std::shared_ptr<core::CancelToken> cancel;
        std::optional<std::uint64_t> job;
    };
    std::shared_ptr<RunState> run_ = std::make_shared<RunState>();
    struct SweepState {
        mutable std::mutex mutex;
        SweepView view;
        std::shared_ptr<core::CancelToken> cancel;
    };
    std::shared_ptr<SweepState> sweep_ = std::make_shared<SweepState>();
    std::shared_ptr<pipeline::ResultCache> cache_;
    int migrated_from_ = pipeline::kSigVersion;
    std::vector<std::string> migration_notes_;
    bool migration_pending_ = false;
};

bool same_port(const pipeline::PortRef& a, const pipeline::PortRef& b);
bool same_wire(const pipeline::Edge& a, const pipeline::Edge& b);

std::string shape_text(const std::vector<pipeline::OutputShape>& shapes, bool compact);

}  // namespace signals::tui
