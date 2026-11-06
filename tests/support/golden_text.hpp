#pragma once

#include <string>
#include <string_view>

namespace signals::test {

void check_golden_text(std::string_view name, const std::string& text);

}  // namespace signals::test
