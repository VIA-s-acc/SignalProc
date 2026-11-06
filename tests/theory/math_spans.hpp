#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "theory/markdown.hpp"

namespace signals::test {

inline std::string_view own_text(std::string_view body) {
    for (std::size_t at = 0; at < body.size();) {
        if (body[at] == '#' && (at == 0 || body[at - 1] == '\n')) return body.substr(0, at);
        const auto next = body.find('\n', at);
        at = next == std::string_view::npos ? body.size() : next + 1;
    }
    return body;
}

}  // namespace signals::test
