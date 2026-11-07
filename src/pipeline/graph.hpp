#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct Source {
    std::size_t node = 0;
    std::size_t output = 0;
    std::string band;
};

struct GraphNode {
    std::string id;
    const BlockType* type = nullptr;
    Params params;
    std::vector<std::optional<PortRef>> inputs;
    std::vector<std::optional<Source>> sources;
    int line = 0;
    bool bypass = false;
};

struct Probe {
    std::string name;
    PortRef at;
    Source source;
};

struct Graph {
    std::vector<GraphNode> nodes;
    std::vector<std::size_t> order;
    std::vector<Probe> probes;
    std::filesystem::path base_dir;
};

struct BuildOptions {
    bool partial = false;
};

std::expected<Graph, SigError> build_graph(const PipelineDoc& doc, std::filesystem::path base_dir, BuildOptions options = {});

}  // namespace signals::pipeline
