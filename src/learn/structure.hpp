#pragma once

#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "learn/task.hpp"
#include "pipeline/sig.hpp"

namespace signals::learn {

using NodeSet = std::set<std::string, std::less<>>;
NodeSet live_nodes(const pipeline::PipelineDoc& doc, std::span<const std::string> outputs);
std::size_t count_blocks(const pipeline::PipelineDoc& doc, std::string_view block, const NodeSet* live = nullptr);
bool path_exists(const pipeline::PipelineDoc& doc, std::string_view from, std::string_view to);
bool all_paths_through(const pipeline::PipelineDoc& doc, const Through& t);
std::vector<std::string> locked_changes(const pipeline::PipelineDoc& start, const pipeline::PipelineDoc& work, std::span<const std::string> locked,
                                        std::string_view input_node);

}  // namespace signals::learn
