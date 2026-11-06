#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"
#include "tui/widgets/format.hpp"

namespace signals::tui {

ftxui::Element status_line(const UiContext& ui, const ResourceSnapshot& r) {
    const auto line = fmt::format(fmt::runtime(tr("tui.status.line")), fmt::arg("cpu", format_number(r.cpu_one_core_pct, 2)),
                                  fmt::arg("all", format_number(r.cpu_all_cores_pct, 2)), fmt::arg("cores", r.core_load_pct.size()),
                                  fmt::arg("rss", format_si(static_cast<double>(r.rss), tr("unit.byte"))),
                                  fmt::arg("peak", format_si(static_cast<double>(r.peak_rss), tr("unit.byte"))),
                                  fmt::arg("threads", r.threads), fmt::arg("fps", format_number(r.fps, 2)));
    return ftxui::text(line) | fg(ui.theme.dim, ui);
}

}  // namespace signals::tui
