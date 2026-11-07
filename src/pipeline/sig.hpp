#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {

struct NodeDecl {
    std::string id, type;
    Params params;
    int line = 0;
    bool bypass = false;
};

struct PortRef {
    std::string node, port;
    int line = 0, column = 0;
};

struct Edge {
    PortRef from, to;
};

struct ProbeDecl {
    PortRef at;
    std::string name;
};

struct PipelineDoc {
    std::string name;
    int version = 1;
    std::vector<NodeDecl> nodes;
    std::vector<Edge> edges;
    std::vector<ProbeDecl> probes;
};

struct SigError {
    int line = 0, column = 0;
    std::string message;
};

std::expected<PipelineDoc, SigError> parse_sig(std::string_view text);
std::string write_sig(const PipelineDoc& doc);
core::Result<void> apply_set(PipelineDoc& doc, std::string_view assignment);
std::optional<std::string> nearest_name(std::string_view name, const std::vector<std::string>& candidates);

}  // namespace signals::pipeline
