#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/value.hpp"

namespace signals::pipeline {

enum class NodeState { waiting, running, done, failed, bypassed, cached };

struct NodeStatus {
    std::string id;
    NodeState state = NodeState::waiting;
    double fraction = 0, cpu_seconds = 0, wall_seconds = 0;
    std::size_t bytes = 0;
    std::string message;
    std::vector<OutputShape> shapes;
};

struct RunEvents {
    std::function<void(const NodeStatus&)> on_node;
};

struct NodeReport {
    std::string node;
    Report report;
};

struct RunResult {
    std::map<std::string, Value, std::less<>> probes;
    std::vector<NodeStatus> nodes;
    std::vector<NodeReport> reports;
    bool checks_passed = true;
};

core::Result<RunResult> run_graph(const Graph& g, ResultCache& cache, const RunEvents& events, const core::CancelToken& cancel);

std::string_view to_string(NodeState state);

}  // namespace signals::pipeline
