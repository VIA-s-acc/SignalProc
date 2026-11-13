#include "pipeline/expr_text.hpp"

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::pipeline {

std::string describe_expr_error(const core::ExprError& e) {
    using K = core::ExprError::Kind;
    const char* key = "expr.error.unexpected";
    switch (e.kind) {
    case K::empty: key = "expr.error.empty"; break;
    case K::unexpected: key = "expr.error.unexpected"; break;
    case K::unexpected_end: key = "expr.error.unexpected_end"; break;
    case K::unknown_function: key = "expr.error.unknown_function"; break;
    case K::bad_arguments: key = "expr.error.bad_arguments"; break;
    case K::unknown_name: key = "expr.error.unknown_name"; break;
    }
    return fmt::format(fmt::runtime(tr(key)), fmt::arg("token", e.token), fmt::arg("pos", e.pos + 1));
}

}  // namespace signals::pipeline
