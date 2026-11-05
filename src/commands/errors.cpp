#include <string>
#include <string_view>

#include <fmt/format.h>

#include "commands/command.hpp"
#include "i18n/i18n.hpp"

namespace signals::cli {
namespace {

std::string_view errc_name(core::Errc code) {
    switch (code) {
    case core::Errc::invalid_argument: return "invalid_argument";
    case core::Errc::level_out_of_range: return "level_out_of_range";
    case core::Errc::length_mismatch: return "length_mismatch";
    case core::Errc::not_perfect_reconstruction: return "not_perfect_reconstruction";
    case core::Errc::unknown_wavelet: return "unknown_wavelet";
    case core::Errc::no_convergence: return "no_convergence";
    case core::Errc::io_error: return "io_error";
    }
    return "invalid_argument";
}

}  // namespace

int report_error(std::ostream& err, const core::Error& error) {
    const auto message =
        fmt::format(fmt::runtime(tr("error." + std::string{errc_name(error.code)})), fmt::arg("details", error.message));
    err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", message)) << '\n';
    return 2;
}

}  // namespace signals::cli
