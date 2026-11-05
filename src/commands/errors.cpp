#include <string>
#include <string_view>

#include <fmt/format.h>

#include "commands/command.hpp"
#include "i18n/i18n.hpp"

namespace signals::cli {
int report_error(std::ostream& err, const core::Error& error) {
    const auto message =
        fmt::format(fmt::runtime(tr("error." + std::string{core::errc_name(error.code)})), fmt::arg("details", error.message));
    err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", message)) << '\n';
    return 2;
}

}  // namespace signals::cli
