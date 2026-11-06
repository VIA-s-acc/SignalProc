#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace signals::theory {

std::u32string to_u32(std::string_view utf8);
std::string to_utf8(std::u32string_view text);
bool is_combining(char32_t c);
std::size_t display_width(std::string_view utf8);
std::string fold_case(std::string_view utf8);

}  // namespace signals::theory
