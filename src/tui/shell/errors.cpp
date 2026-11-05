#include "tui/shell/errors.hpp"

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::tui {

std::string error_text(const core::Error& error) {
    const auto message =
        fmt::format(fmt::runtime(tr("error." + std::string{core::errc_name(error.code)})), fmt::arg("details", error.message));
    return fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", message));
}

}  // namespace signals::tui
