#pragma once

#include <string>
#include <string_view>

namespace signals::pipeline {

std::string markdown_cell(std::string_view text);
std::string blocks_markdown();
std::string blocks_text();
std::string blocks_json();

}  // namespace signals::pipeline
