#include "tui/run.hpp"

#include <memory>
#include <utility>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/screens/filters_model.hpp"
#include "tui/screens/filters_screen.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/screens/signal_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {

int run(std::optional<std::filesystem::path> file, std::string_view theme) {
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
    auto signal = std::make_shared<SignalModel>();
    auto comparison = std::make_shared<ComparisonSet>();
    for (auto role : {FilterRole::h0, FilterRole::h1}) (void)comparison->add({FilterRef::Kind::bank, "db2", role});
    JobRunner jobs{[&app] { app.PostEvent(ftxui::Event::Custom); }};
    audio::Player player{audio::make_device_sink()};
    Ticker ticker{[&app] { app.PostEvent(ftxui::Event::Custom); }};
    ui.player = &player;
    ui.ticker = &ticker;
    ResourceMonitor monitor{[&app] { app.PostEvent(ftxui::Event::Custom); }};
    monitor.set_queue_probe([&jobs] { return jobs.queued(); });
    ui.jobs = &jobs;
    ui.monitor = &monitor;
    ui.resources = [&monitor] { return monitor.latest(); };
    ui.history = [&monitor] { return monitor.history(); };
    ui.prefs_file = prefs_path(env);
    ui.first_run_check = !prefs || !prefs->checked;
    ui.open_file = [signal, &jobs](const std::filesystem::path& path) { signal->open(path, jobs); };
    auto screens = placeholder_screens(ui);
    screens[0] = make_signal_screen(ui, signal);
    screens[2] = make_filters_screen(ui, comparison);
    if (file) signal->open(*file, jobs);
    auto shell = make_shell(ui, std::move(screens), [&app] { app.Exit(); });
    app.Loop(shell);
    return 0;
}

}  // namespace signals::tui
