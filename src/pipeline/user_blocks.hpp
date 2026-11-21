#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "io/config_dir.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct LibraryBlock {
    PipelineDoc doc;
    std::filesystem::path file;
};

std::optional<std::filesystem::path> user_blocks_dir(const io::EnvLookup& env);
std::vector<std::filesystem::path> user_block_files(const io::EnvLookup& env);
std::optional<LibraryBlock> find_user_block(std::string_view name, const io::EnvLookup& env);
std::vector<BlockDef> load_user_blocks(const std::filesystem::path& dir);

}  // namespace signals::pipeline
