#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pipeline/block.hpp"

namespace signals::test {

struct BlockGraph {
    std::string name;
    std::string sig;
};

void prepare_block_files(const std::filesystem::path& dir);
std::vector<BlockGraph> block_graphs(const pipeline::BlockType& t, const std::filesystem::path& dir);

}  // namespace signals::test
