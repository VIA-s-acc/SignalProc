#pragma once

#include <string>
#include <vector>

#include "pipeline/graph.hpp"

namespace signals::pipeline {

std::vector<std::string> draw_graph(const Graph& g, bool ascii, int max_width = 120);

}  // namespace signals::pipeline
