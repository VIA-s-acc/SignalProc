#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {

struct Comments {
    bool gap = false;
    std::vector<std::string> above = {};
    std::string after = {};
    bool empty() const { return !gap && above.empty() && after.empty(); }
};

struct NodeDecl {
    std::string id, type;
    Params params;
    int line = 0;
    bool bypass = false;
    Comments comments = {};
};

struct PortRef {
    std::string node, port;
    int line = 0, column = 0;
};

struct Edge {
    PortRef from, to;
    Comments comments = {};
};

struct ProbeDecl {
    PortRef at;
    std::string name;
    Comments comments = {};
};

struct PipelineDoc {
    std::string name;
    int version = 1;
    std::vector<NodeDecl> nodes;
    std::vector<Edge> edges;
    std::vector<ProbeDecl> probes;
    std::vector<std::string> preamble = {};
    std::string header_comment = {};
    Comments tail = {};
};

struct SigError {
    int line = 0, column = 0;
    std::string message;
};

std::expected<PipelineDoc, SigError> parse_sig(std::string_view text);
std::string write_sig(const PipelineDoc& doc);
void remove_edges(PipelineDoc& doc, const std::function<bool(const Edge&)>& gone);
void remove_node(PipelineDoc& doc, std::string_view id);
core::Result<void> apply_set(PipelineDoc& doc, std::string_view assignment);
std::optional<std::string> nearest_name(std::string_view name, const std::vector<std::string>& candidates);

}  // namespace signals::pipeline
