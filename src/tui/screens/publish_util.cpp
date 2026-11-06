#include "tui/screens/publish_util.hpp"

#include <utility>

#include "i18n/i18n.hpp"

namespace signals::tui {

std::string subscript_number(std::size_t n) {
    static const char* digits[]{"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
    const auto s = std::to_string(n);
    std::string out;
    for (char c : s) out += digits[c - '0'];
    return out;
}

std::string yes_no(bool v) { return tr(v ? "tui.theory.value.yes" : "tui.theory.value.no"); }

std::string joined(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += sep;
        out += p;
    }
    return out;
}

core::CheckResult renamed(core::CheckResult r, std::string id) {
    r.id = std::move(id);
    return r;
}

}  // namespace signals::tui
