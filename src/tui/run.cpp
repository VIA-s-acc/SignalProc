#include "tui/run.hpp"

#include <utility>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/shell/shell.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {

int run([[maybe_unused]] std::optional<std::filesystem::path> file, std::string_view theme) {
    const auto env = process_env();
    const auto prefs = load_prefs(prefs_path(env));
    auto app = ftxui::App::Fullscreen();
    UiContext ui;
    ui.theme = builtin_theme(theme);
    ui.caps = apply(detect_caps(env), prefs ? *prefs : TerminalPrefs{});
    ui.terminal_size = [] {
        const auto d = ftxui::Terminal::Size();
        return std::pair{d.dimx, d.dimy};
    };
    JobRunner jobs{[&app] { app.PostEvent(ftxui::Event::Custom); }};
    ResourceMonitor monitor{[&app] { app.PostEvent(ftxui::Event::Custom); }};
    monitor.set_queue_probe([&jobs] { return jobs.queued(); });
    ui.jobs = &jobs;
    ui.monitor = &monitor;
    ui.resources = [&monitor] { return monitor.latest(); };
    auto shell = make_shell(ui, placeholder_screens(ui), [&app] { app.Exit(); });
    app.Loop(shell);
    return 0;
}

}  // namespace signals::tui
