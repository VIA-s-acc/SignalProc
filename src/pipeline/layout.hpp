#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pipeline/graph.hpp"

namespace signals::pipeline {

struct LayoutItem {
    int node = -1;
    int layer = 0;
    int order = 0;
    std::vector<std::size_t> next;
};

struct GraphLayout {
    std::vector<LayoutItem> items;
    std::vector<std::vector<std::size_t>> layers;
    std::vector<std::size_t> item_of_node;
};

GraphLayout layout_graph(const Graph& g);
int crossings(const GraphLayout& l);

std::vector<std::string> draw_graph(const Graph& g, bool ascii, int max_width = 120);

}  // namespace signals::pipeline
