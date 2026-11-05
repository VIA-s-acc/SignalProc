#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace signals::test {

nlohmann::json load_golden(std::string_view file_name);

std::vector<double> golden_signal(const nlohmann::json& dwt_json, std::size_t n);

}  // namespace signals::test
