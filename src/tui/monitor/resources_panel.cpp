#include "core/version.hpp"
#include "tui/monitor/resources_panel.hpp"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/monitor/sampler.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {
namespace {

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kAddressSanitizer = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kAddressSanitizer = true;
#else
constexpr bool kAddressSanitizer = false;
#endif
#else
constexpr bool kAddressSanitizer = false;
#endif

#ifdef NDEBUG
constexpr bool kOptimised = true;
#else
constexpr bool kOptimised = false;
#endif

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

ftxui::Element resources_panel(const UiContext& ui, int width, int height, float scroll) {
    using namespace ftxui;
    const auto& th = ui.theme;
    const auto history = ui.history ? ui.history() : std::vector<ResourceSnapshot>{};
    const auto latest = ui.resources ? ui.resources() : (history.empty() ? ResourceSnapshot{} : history.back());
    const bool wide = width >= 140;
    const int column = wide ? 70 : std::max(30, width - 6);
    const auto heading = [&](const std::string& s) { return text(s) | bold; };
    const auto row = [&](const std::string& key, const std::string& value) {
        return hbox({text(key) | fg(th.dim, ui) | size(WIDTH, EQUAL, 12), paragraph(value)});
    };

    const auto ram = physical_memory_bytes();
    const auto cores = latest.core_load_pct.empty() ? std::size_t{1} : latest.core_load_pct.size();
    Elements now{heading(tr("tui.resources.now"))};
    now.push_back(row(tr("tui.resources.cpu_row"), fmt::format(fmt::runtime(tr("tui.resources.cpu_value")), fmt::arg("one", format_number(latest.cpu_one_core_pct, 2)),
                                                                fmt::arg("all", format_number(latest.cpu_all_cores_pct, 2)), fmt::arg("cores", cores),
                                                                fmt::arg("seconds", format_number(latest.cpu_seconds, 3)))));
    now.push_back(row(tr("tui.resources.memory_row"),
                      fmt::format(fmt::runtime(tr("tui.resources.memory_value")), fmt::arg("rss", format_si(static_cast<double>(latest.rss), "Б")),
                                  fmt::arg("peak", format_si(static_cast<double>(latest.peak_rss), "Б")),
                                  fmt::arg("share", format_number(ram ? 100.0 * static_cast<double>(latest.rss) / static_cast<double>(ram) : 0.0, 2)))));
    now.push_back(row(tr("tui.resources.threads_row"), std::to_string(latest.threads)));
    now.push_back(row(tr("tui.resources.disk_row"),
                      fmt::format(fmt::runtime(tr("tui.resources.disk_value")), fmt::arg("read", format_si(static_cast<double>(latest.disk_read_bytes), "Б")),
                                  fmt::arg("read_rate", format_si(latest.disk_read_rate, "Б/с")),
                                  fmt::arg("written", format_si(static_cast<double>(latest.disk_written_bytes), "Б")),
                                  fmt::arg("write_rate", format_si(latest.disk_write_rate, "Б/с")))));
    now.push_back(row(tr("tui.resources.frames_row"), format_number(latest.fps, 2) + " " + tr("tui.resources.fps")));
    now.push_back(row(tr("tui.resources.build_row"), "Signals " + std::string{core::version()} + " · " +
                                                      tr(kAddressSanitizer ? "tui.resources.build_asan" : kOptimised ? "tui.resources.build_release" : "tui.resources.build_debug")) |
                  fg(kAddressSanitizer ? th.warn : th.fg, ui));

    std::vector<double> cpu, mem;
    double cpu_max = 100, mem_max = 1;
    for (const auto& s : history) {
        cpu.push_back(s.cpu_one_core_pct);
        mem.push_back(static_cast<double>(s.rss) / (1 << 20));
        cpu_max = std::max(cpu_max, s.cpu_one_core_pct);
        mem_max = std::max(mem_max, mem.back());
    }
    const double span = std::max<double>(1, static_cast<double>(history.size()) - 1);
    const int plot_rows = wide ? 8 : 6;
    PlotSpec cpu_plot{{0, span}, {0, cpu_max}, {Trace{cpu, {}, th.trace[0]}}};
    PlotSpec mem_plot{{0, span}, {0, mem_max * 1.1}, {Trace{mem, {}, th.trace[2]}}};
    Elements plots{heading(tr("tui.resources.cpu")), plot(cpu_plot, th, ui.caps.color, ui.caps.glyphs) | size(HEIGHT, EQUAL, plot_rows),
                   heading(tr("tui.resources.memory")), plot(mem_plot, th, ui.caps.color, ui.caps.glyphs) | size(HEIGHT, EQUAL, plot_rows)};

    const int bar = wide ? 10 : std::max(6, column - 42);
    Elements threads{heading(tr("tui.resources.threads"))};
    for (const auto& t : latest.thread_load) {
        std::string name = t.name;
        if (name.starts_with("signals-")) {
            const auto key = "tui.resources.thread." + name.substr(8);
            const auto label = tr(key);
            name = label == key ? name : label;
        }
        if (name.empty()) name = tr("tui.resources.thread.system");
        threads.push_back(hbox({text(name) | size(WIDTH, EQUAL, 18), text(eighths_bar(std::min(1.0, t.pct / 100), bar)) | fg(th.trace[0], ui),
                                text(" " + format_number(t.pct, 2) + " %") | size(WIDTH, EQUAL, 10),
                                text(fmt::format(fmt::runtime(tr("tui.resources.thread_total")), fmt::arg("seconds", format_number(t.cpu_seconds, 3)))) |
                                    fg(th.dim, ui)}));
    }
    if (latest.thread_load.empty()) threads.push_back(text(tr("tui.resources.threads_unknown")) | fg(th.dim, ui));

    Elements buffers{heading(tr("tui.resources.buffers"))};
    for (std::size_t i = 0; i < latest.buffers.size(); ++i) {
        const bool untracked = static_cast<core::MemoryCategory>(i) == core::MemoryCategory::fft_plans;
        buffers.push_back(hbox({text(buffer_name(i)) | size(WIDTH, EQUAL, 22),
                                text(untracked ? tr("tui.resources.untracked") : format_si(static_cast<double>(latest.buffers[i]), "Б"))}));
    }
    Elements queue{heading(tr("tui.resources.queue"))};
    if (ui.jobs)
        for (const auto& j : ui.jobs->snapshot()) {
            if (j.finished) continue;
            std::string stage = "—";
            double fraction = 0;
            for (const auto& s : j.stages)
                if (s.state == StageState::running) stage = s.name, fraction = s.fraction;
            queue.push_back(hbox({text(j.title + " · " + stage) | flex, text(" " + format_number(100 * fraction, 2) + " %")}));
        }
    if (queue.size() == 1) queue.push_back(text(tr("tui.resources.idle")) | fg(th.dim, ui));

    Elements system{heading(tr("tui.resources.cores")), text(tr("tui.resources.cores_note")) | fg(th.dim, ui)};
    const int per_line = wide ? 1 : std::max(1, column / 22);
    Elements line;
    for (std::size_t i = 0; i < latest.core_load_pct.size(); ++i) {
        line.push_back(hbox({text(std::to_string(i + 1)) | size(WIDTH, EQUAL, 3), text(eighths_bar(latest.core_load_pct[i] / 100, wide ? 16 : 8)) | fg(th.dim, ui),
                             text(" " + format_number(latest.core_load_pct[i], 2) + " %") | size(WIDTH, EQUAL, 9)}));
        if (static_cast<int>(line.size()) == per_line || i + 1 == latest.core_load_pct.size()) {
            system.push_back(hbox(std::move(line)));
            line.clear();
        }
    }

    const auto gap = [] { return text(""); };
    Element body;
    if (wide) {
        auto left = vbox({vbox(std::move(now)), gap(), vbox(std::move(plots))}) | size(WIDTH, EQUAL, 70);
        auto right = vbox({vbox(std::move(threads)), gap(), vbox(std::move(buffers)), gap(), vbox(std::move(queue)), gap(), vbox(std::move(system))}) |
                     size(WIDTH, EQUAL, 60);
        body = hbox({left, text("  "), right});
    } else {
        body = vbox({vbox(std::move(now)), gap(), vbox(std::move(threads)), gap(), vbox(std::move(plots)), gap(), vbox(std::move(buffers)), gap(),
                     vbox(std::move(queue)), gap(), vbox(std::move(system))}) |
               size(WIDTH, EQUAL, column);
    }
    body = body | focusPositionRelative(0.f, scroll) | yframe | size(HEIGHT, LESS_THAN, std::max(4, height - 4));
    return panel(tr("tui.resources.title"), {{tr("tui.resources.close"), false}}, body, true, ui);
}

}  // namespace signals::tui
