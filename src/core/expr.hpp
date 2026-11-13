#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace signals::core {

struct ExprError {
    enum class Kind { empty, unexpected, unexpected_end, unknown_function, bad_arguments, unknown_name };
    Kind kind = Kind::unexpected;
    std::size_t pos = 0;
    std::string token = {};
};

template <class T>
using ExprResult = std::expected<T, ExprError>;

struct Expr {
    enum class Op { number, name, neg, plus, not_, add, sub, mul, div, pow, lt, le, gt, ge, eq, ne, and_, or_, call };
    struct Node {
        Op op = Op::number;
        double value = 0;
        std::string name = {};
        std::size_t pos = 0;
        std::vector<std::size_t> args = {};
    };
    std::vector<Node> nodes;
    std::size_t root = 0;
};

using NameLookup = std::function<std::optional<double>(std::string_view)>;

ExprResult<Expr> parse_expr(std::string_view text);
ExprResult<double> eval(const Expr& e, const NameLookup& names);
ExprResult<double> eval_number(std::string_view text);
std::vector<std::string> expr_names(const Expr& e);

}  // namespace signals::core
