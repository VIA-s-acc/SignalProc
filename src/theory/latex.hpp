#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace signals::theory {

struct MathNode {
    enum class Kind { row, symbol, text, frac, sqrt, scripts, big_op, fenced, matrix, accent, function, space };
    Kind kind = Kind::row;
    std::string text;
    std::string ascii;
    std::vector<MathNode> children;
    bool has_sub = false, has_sup = false;
    std::string left, right;
    std::size_t columns = 0;
    bool limits = false;
    int size = 0;
};

struct LatexError {
    std::size_t offset = 0;
    std::string message;
};

std::expected<MathNode, LatexError> parse_latex(std::string_view src);
std::vector<std::string_view> supported_commands();

}  // namespace signals::theory
