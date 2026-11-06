#include "tui/run.hpp"

#include <cstdio>
#include <memory>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include "i18n/i18n.hpp"
#include "tui/app_screens.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/shell/shell.hpp"
#include "tui/term/caps.hpp"
#include "tui/term/external.hpp"

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
    ui.user_filters = user_filters_dir(env);
    ui.repo_root = SIGNALS_SOURCE_DIR;
    ui.open_in_editor = [&app, env](std::filesystem::path path) {
        app.WithRestoredIO([argv = editor_command(path, env)] { launch(argv); })();
    };
    ui.copy_text = [](std::string text) {
        std::fputs(osc52(text).c_str(), stdout);
        std::fflush(stdout);
    };
    const auto library = load_user_banks(ui.user_filters);
    set_user_banks(library.banks);
    auto models = make_app_models();
    for (const auto& e : library.errors)
        models.signal->add_log(fmt::format(fmt::runtime(tr("tui.user_filters.broken")), fmt::arg("error", e)), LogLine::warn);
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
    ui.warn = [signal = models.signal](std::string line) { signal->add_log(std::move(line), LogLine::warn); };
    ui.open_file = [signal = models.signal, &jobs](const std::filesystem::path& path) { signal->open(path, jobs); };
    auto screens = make_app_screens(ui, models);
    if (file) models.signal->open(*file, jobs);
    auto shell = make_shell(ui, std::move(screens), [&app] { app.Exit(); });
    app.Loop(shell);
    return 0;
}

}  // namespace signals::tui
