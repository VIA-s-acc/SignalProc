#pragma once

#include <span>
#include <string_view>

namespace signals::pipeline {

std::span<const std::string_view> report_fields(std::string_view block);
bool field_matches(std::string_view pattern, std::string_view field);

}  // namespace signals::pipeline
