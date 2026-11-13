#include "pipeline/executor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <new>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

double thread_cpu_seconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    const auto ticks = [](const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;
#else
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#endif
}

struct Fnv {
    std::uint64_t h = 1469598103934665603ULL;
    void bytes(const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    }
    void number(std::uint64_t v) { bytes(&v, sizeof v); }
    void text(std::string_view s) {
        number(s.size());
        bytes(s.data(), s.size());
    }
};

std::uint64_t node_key(const Graph& g, std::size_t i, const std::vector<std::uint64_t>& keys) {
    const auto& n = g.nodes[i];
    Fnv f;
    f.text(n.type->id);
    f.number(n.bypass ? 1 : 0);
    for (const auto& spec : n.type->params) {
        const auto it = n.params.find(spec.name);
        const std::string value = it == n.params.end() ? spec.fallback : it->second;
        f.text(spec.name);
        f.text(value);
        if (spec.kind != ParamSpec::Kind::path) continue;
        auto path = utf8_path(value);
        if (path.is_relative()) path = g.base_dir / path;
        f.text(path.lexically_normal().string());
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        f.number(ec ? 0 : size);
        const auto stamp = std::filesystem::last_write_time(path, ec);
        f.number(ec ? 0 : static_cast<std::uint64_t>(stamp.time_since_epoch().count()));
    }
    for (const auto& s : n.sources) {
        if (!s) {
            f.number(0);
            continue;
        }
        f.number(keys[s->node]);
        f.number(s->output);
        f.text(s->band);
    }
    return f.h;
}

class NodeProgress final : public core::ProgressSink {
public:
    NodeProgress(const core::CancelToken& cancel, const std::atomic<bool>& stop, std::function<void(double)> on_fraction)
        : cancel_{cancel}, stop_{stop}, on_fraction_{std::move(on_fraction)} {}
    void report(double fraction) override {
        fraction = std::clamp(fraction, 0.0, 1.0);
        if (fraction < last_ + 0.01 && fraction < 1.0) return;
        if (fraction <= last_) return;
        last_ = fraction;
        on_fraction_(fraction);
    }
    bool cancelled() const override { return cancel_.cancelled() || stop_.load(std::memory_order_relaxed); }

private:
    const core::CancelToken& cancel_;
    const std::atomic<bool>& stop_;
    std::function<void(double)> on_fraction_;
    double last_ = 0;
};

class Run {
public:
    Run(const Graph& g, ResultCache& cache, const RunEvents& events, const core::CancelToken& cancel)
        : g_{g}, cache_{cache}, events_{events}, cancel_{cancel}, keys_(g.nodes.size()), outputs_(g.nodes.size()), fresh_(g.nodes.size()),
          status_(g.nodes.size()) {
        for (const auto i : g.order) keys_[i] = node_key(g, i, keys_);
        for (std::size_t i = 0; i < g.nodes.size(); ++i) status_[i].id = g.nodes[i].id;
        pending_.assign(g.nodes.size(), 0);
        probed_.assign(g.nodes.size(), false);
        for (const auto& n : g.nodes) {
            std::set<std::size_t> from;
            for (const auto& s : n.sources)
                if (s) {
                    ++readers_[{s->node, s->output}];
                    from.insert(s->node);
                }
            for (const auto f : from) ++pending_[f];
        }
        for (const auto& p : g.probes) {
            readers_[{p.source.node, p.source.output}] += 2;
            probed_[p.source.node] = true;
        }
    }

    core::Result<RunResult> execute() {
        for (std::size_t i = 0; i < g_.nodes.size(); ++i) emit(i);
        tf::Taskflow flow;
        std::vector<tf::Task> tasks;
        tasks.reserve(g_.nodes.size());
        for (std::size_t i = 0; i < g_.nodes.size(); ++i) tasks.push_back(flow.emplace([this, i] { node(i); }));
        for (std::size_t i = 0; i < g_.nodes.size(); ++i) {
            std::set<std::size_t> before;
            for (const auto& s : g_.nodes[i].sources)
                if (s) before.insert(s->node);
            for (const auto b : before) tasks[b].precede(tasks[i]);
        }
        const auto workers = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, std::max<std::size_t>(1, g_.nodes.size()));
        tf::Executor executor{workers};
        executor.run(flow).wait();

        if (cancel_.cancelled()) return core::make_error(core::Errc::cancelled, tr("pipeline.run.cancelled"));
        if (error_) return std::unexpected{*error_};
        return collect();
    }

private:
    void emit(std::size_t i) {
        NodeStatus copy;
        {
            std::lock_guard lock{mutex_};
            copy = status_[i];
        }
        if (events_.on_node) events_.on_node(copy);
    }

    void set(std::size_t i, NodeState state, std::string message = {}) {
        {
            std::lock_guard lock{mutex_};
            status_[i].state = state;
            if (state == NodeState::done || state == NodeState::cached) status_[i].fraction = 1;
            if (!message.empty()) status_[i].message = std::move(message);
        }
        emit(i);
    }

    void fail(std::size_t i, const core::Error& e) {
        const auto& n = g_.nodes[i];
        {
            std::lock_guard lock{mutex_};
            if (!error_)
                error_ = core::Error{e.code, fmt::format(fmt::runtime(tr("pipeline.run.node_failed")), fmt::arg("node", n.id),
                                                         fmt::arg("line", n.line), fmt::arg("message", e.message))};
        }
        stop_.store(true);
        set(i, NodeState::failed, e.message);
    }

    void node(std::size_t i) {
        run_node(i);
        std::set<std::size_t> from;
        for (const auto& s : g_.nodes[i].sources)
            if (s) from.insert(s->node);
        std::vector<Outputs> released;
        std::vector<std::shared_ptr<std::vector<Value>>> released_fresh;
        std::lock_guard lock{mutex_};
        for (const auto f : from)
            if (--pending_[f] == 0 && !probed_[f]) {
                released.push_back(std::move(outputs_[f]));
                released_fresh.push_back(std::move(fresh_[f]));
            }
    }

    void run_node(std::size_t i) {
        const auto& n = g_.nodes[i];
        if (cancel_.cancelled() || stop_.load()) return set(i, NodeState::bypassed);
        if (!n.type->side_effect)
            if (auto hit = cache_.get(keys_[i])) {
                outputs_[i] = std::move(hit);
                {
                    std::lock_guard lock{mutex_};
                    status_[i].bytes = outputs_bytes(*outputs_[i]);
            status_[i].shapes.clear();
            for (const auto& v : *outputs_[i]) status_[i].shapes.push_back(shape_of(v));
                }
                return set(i, NodeState::cached);
            }
        std::vector<Value> copies;
        std::span<const Value> inputs;
        std::vector<bool> owned;
        const bool direct = n.sources.size() == 1 && n.sources[0] && n.sources[0]->band.empty();
        if (direct && stealable(*n.sources[0])) {
            copies.push_back(std::move((*fresh_[n.sources[0]->node])[n.sources[0]->output]));
            inputs = copies;
            owned = {true};
        } else if (direct) {
            const auto& from = outputs_[n.sources[0]->node];
            if (!from) return set(i, NodeState::bypassed);
            inputs = std::span<const Value>{&(*from)[n.sources[0]->output], 1};
        } else {
            for (std::size_t k = 0; k < n.sources.size(); ++k) {
                const auto& s = n.sources[k];
                if (!s) {
                    copies.emplace_back();
                    continue;
                }
                const auto& from = outputs_[s->node];
                if (!from) return set(i, NodeState::bypassed);
                const auto& value = (*from)[s->output];
                if (s->band.empty()) {
                    copies.push_back(value);
                    continue;
                }
                auto band = band_port(std::get<Coeffs>(value), s->band);
                if (!band)
                    return fail(i, {core::Errc::invalid_argument,
                                    fmt::format(fmt::runtime(tr("pipeline.run.no_band")), fmt::arg("band", s->band), fmt::arg("node", g_.nodes[s->node].id))});
                copies.push_back(std::move(*band));
            }
            inputs = copies;
        }
        if (n.bypass) {
            auto passed = std::make_shared<std::vector<Value>>(n.type->outputs.size(), inputs.empty() ? Value{} : inputs.front());
            {
                std::lock_guard lock{mutex_};
                status_[i].bytes = outputs_bytes(*passed);
            status_[i].shapes.clear();
            for (const auto& v : *passed) status_[i].shapes.push_back(shape_of(v));
                status_[i].fraction = 1;
            }
            outputs_[i] = passed;
            fresh_[i] = std::move(passed);
            return set(i, NodeState::bypassed);
        }
        set(i, NodeState::running);
        NodeProgress progress{cancel_, stop_, [this, i](double f) {
                                  {
                                      std::lock_guard lock{mutex_};
                                      status_[i].fraction = f;
                                  }
                                  emit(i);
                              }};
        const auto wall0 = std::chrono::steady_clock::now();
        const auto cpu0 = thread_cpu_seconds();
        core::Result<std::vector<Value>> result = std::vector<Value>{};
        try {
            result = n.type->run(RunContext{g_.base_dir, &progress, std::move(owned)}, inputs, n.params);
        } catch (const std::bad_alloc&) {
            result = core::make_error(core::Errc::invalid_argument, tr("pipeline.run.out_of_memory"));
        } catch (const std::exception& e) {
            result = core::make_error(core::Errc::invalid_argument, e.what());
        }
        {
            std::lock_guard lock{mutex_};
            status_[i].cpu_seconds = thread_cpu_seconds() - cpu0;
            status_[i].wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
        }
        if (!result) {
            if (result.error().code == core::Errc::cancelled || progress.cancelled()) return set(i, NodeState::bypassed, result.error().message);
            return fail(i, result.error());
        }
        auto produced = std::make_shared<std::vector<Value>>(std::move(*result));
        {
            std::lock_guard lock{mutex_};
            status_[i].bytes = outputs_bytes(*produced);
            status_[i].shapes.clear();
            for (const auto& v : *produced) status_[i].shapes.push_back(shape_of(v));
        }
        if (!n.type->side_effect) cache_.put(keys_[i], produced);
        outputs_[i] = produced;
        fresh_[i] = std::move(produced);
        set(i, NodeState::done);
    }

    bool stealable(const Source& s) const {
        const auto it = readers_.find({s.node, s.output});
        // outputs_ and fresh_ hold one reference each; a third one means the cache kept the result.
        return it != readers_.end() && it->second == 1 && fresh_[s.node] && fresh_[s.node].use_count() == 2;
    }

    core::Result<RunResult> collect() {
        RunResult out;
        out.nodes = status_;
        for (const auto i : g_.order) {
            if (!outputs_[i]) continue;
            for (const auto& v : *outputs_[i])
                if (const auto* report = std::get_if<Report>(&v)) {
                    out.reports.push_back({g_.nodes[i].id, *report});
                    if ((g_.nodes[i].type->id == "check" || g_.nodes[i].type->id == "assert") && !report->passed) out.checks_passed = false;
                }
        }
        for (const auto& p : g_.probes) {
            const auto& from = outputs_[p.source.node];
            if (!from || p.source.output >= from->size()) continue;
            const auto& value = (*from)[p.source.output];
            if (p.source.band.empty()) {
                out.probes.emplace(p.name, value);
                continue;
            }
            auto band = band_port(std::get<Coeffs>(value), p.source.band);
            if (!band)
                return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.run.no_band")), fmt::arg("band", p.source.band),
                                                                                  fmt::arg("node", g_.nodes[p.source.node].id)));
            out.probes.emplace(p.name, std::move(*band));
        }
        return out;
    }

    const Graph& g_;
    ResultCache& cache_;
    const RunEvents& events_;
    const core::CancelToken& cancel_;
    std::vector<std::uint64_t> keys_;
    std::vector<Outputs> outputs_;
    std::vector<std::shared_ptr<std::vector<Value>>> fresh_;
    std::map<std::pair<std::size_t, std::size_t>, int> readers_;
    std::vector<int> pending_;
    std::vector<bool> probed_;
    std::vector<NodeStatus> status_;
    std::mutex mutex_;
    std::atomic<bool> stop_{false};
    std::optional<core::Error> error_;
};

}  // namespace

core::Result<RunResult> run_graph(const Graph& g, ResultCache& cache, const RunEvents& events, const core::CancelToken& cancel) {
    return Run{g, cache, events, cancel}.execute();
}

std::string_view to_string(NodeState state) {
    switch (state) {
    case NodeState::waiting: return "waiting";
    case NodeState::running: return "running";
    case NodeState::done: return "done";
    case NodeState::failed: return "failed";
    case NodeState::bypassed: return "bypassed";
    case NodeState::cached: return "cached";
    }
    return "?";
}

}  // namespace signals::pipeline
