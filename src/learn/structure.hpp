#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "learn/task.hpp"
#include "pipeline/sig.hpp"

namespace signals::learn {

std::size_t count_blocks(const pipeline::PipelineDoc& doc, std::string_view block);
bool all_paths_through(const pipeline::PipelineDoc& doc, const Through& t);
std::vector<std::string> locked_changes(const pipeline::PipelineDoc& start, const pipeline::PipelineDoc& work, std::span<const std::string> locked,
                                        std::string_view input_node);

}  // namespace signals::learn
