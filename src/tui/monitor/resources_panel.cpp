#include "tui/monitor/resources_panel.hpp"

#include <algorithm>
#include <string>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {
namespace {

std::string buffer_name(std::size_t i) {
    switch (static_cast<core::MemoryCategory>(i)) {
    case core::MemoryCategory::signals: return tr("tui.resources.signals");
    case core::MemoryCategory::coefficients: return tr("tui.resources.coefficients");
    case core::MemoryCategory::pipeline_cache: return tr("tui.resources.pipeline");
    case core::MemoryCategory::fft_plans: return tr("tui.resources.fft");
    case core::MemoryCategory::display: return tr("tui.resources.display");
    case core::MemoryCategory::count: break;
    }
    return {};
}

}  // namespace

ftxui::Element resources_panel(const UiContext& ui) {
    using namespace ftxui;
    const auto history = ui.history ? ui.history() : std::vector<ResourceSnapshot>{};
    const auto latest = ui.resources ? ui.resources() : (history.empty() ? ResourceSnapshot{} : history.back());
    std::vector<double> cpu, mem;
    double cpu_max = 100, mem_max = 1;
    for (const auto& s : history) {
        cpu.push_back(s.cpu_one_core_pct);
        mem.push_back(static_cast<double>(s.rss) / (1 << 20));
        cpu_max = std::max(cpu_max, s.cpu_one_core_pct);
        mem_max = std::max(mem_max, mem.back());
    }
    const double span = std::max<double>(1, static_cast<double>(history.size()) - 1);
    PlotSpec cpu_plot{{0, span}, {0, cpu_max}, {Trace{cpu, {}, ui.theme.trace[0]}}};
    PlotSpec mem_plot{{0, span}, {0, mem_max * 1.1}, {Trace{mem, {}, ui.theme.trace[2]}}};
    Elements cores;
    for (std::size_t i = 0; i < latest.core_load_pct.size(); ++i)
        cores.push_back(hbox({text(std::to_string(i + 1)) | size(WIDTH, EQUAL, 3), text(eighths_bar(latest.core_load_pct[i] / 100, 16)) | fg(ui.theme.trace[0], ui),
                              text(" " + format_number(latest.core_load_pct[i], 2) + " %")}));
    Elements buffers;
    for (std::size_t i = 0; i < latest.buffers.size(); ++i) {
        const bool untracked = static_cast<core::MemoryCategory>(i) == core::MemoryCategory::fft_plans;
        buffers.push_back(hbox({text(buffer_name(i)) | size(WIDTH, EQUAL, 22),
                                text(untracked ? tr("tui.resources.untracked") : format_si(static_cast<double>(latest.buffers[i]), "Б"))}));
    }
    Elements queue;
    if (ui.jobs)
        for (const auto& j : ui.jobs->snapshot()) {
            if (j.finished) continue;
            std::string stage = "—";
            double fraction = 0;
            for (const auto& s : j.stages)
                if (s.state == StageState::running) stage = s.name, fraction = s.fraction;
            queue.push_back(hbox({text(j.title + " · " + stage) | flex, text(" " + format_number(100 * fraction, 2) + " %")}));
        }
    if (queue.empty()) queue.push_back(text(tr("tui.resources.idle")) | fg(ui.theme.dim, ui));
    auto left = vbox({text(tr("tui.resources.cpu")) | bold, plot(cpu_plot, ui.theme, ui.caps.color, ui.caps.glyphs) | size(HEIGHT, EQUAL, 8),
                      text(tr("tui.resources.memory")) | bold, plot(mem_plot, ui.theme, ui.caps.color, ui.caps.glyphs) | size(HEIGHT, EQUAL, 8)}) |
                size(WIDTH, EQUAL, 70);
    auto right = vbox({text(tr("tui.resources.cores")) | bold, vbox(std::move(cores)), text(""), text(tr("tui.resources.buffers")) | bold,
                       vbox(std::move(buffers)), text(""), text(tr("tui.resources.queue")) | bold, vbox(std::move(queue))}) |
                 size(WIDTH, EQUAL, 46);
    return panel(tr("tui.resources.title"), {{tr("tui.resources.close"), false}}, hbox({left, text("  "), right}), true, ui);
}

}  // namespace signals::tui
