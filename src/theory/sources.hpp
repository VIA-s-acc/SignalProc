#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace signals::theory {

std::optional<std::string_view> embedded_source(std::string_view path);
std::optional<std::size_t> source_line(std::string_view text, std::string_view function);

}  // namespace signals::theory
