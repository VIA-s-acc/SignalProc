#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace signals::pipeline {

std::string markdown_cell(std::string_view text);
std::string blocks_markdown();
std::string blocks_text();
struct BlockType;

std::string blocks_json(const std::function<std::string(const BlockType&)>& python = {});

}  // namespace signals::pipeline
