#include "tui/run.hpp"

#include <algorithm>
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
#include "learn/task.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/data/session.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/shell/shell.hpp"
#include "tui/term/caps.hpp"
#include "tui/term/external.hpp"

namespace signals::tui {

void load_user_wavelets() {
    set_user_banks(load_user_banks(user_filters_dir(process_env())).banks);
    install_user_wavelets();
}

int run(std::optional<std::filesystem::path> file, std::string_view theme, std::optional<Language> language) {
    const auto env = process_env();
    const auto prefs = load_prefs(prefs_path(env));
    if (!language)
        if (auto code = env("SIGNALS_LANG")) language = parse_language(*code);
    if (!language && prefs) language = prefs->language;
    set_language(language.value_or(Language::ru));
    auto app = ftxui::App::Fullscreen();
    UiContext ui;
    auto session = std::make_shared<SessionStore>(session_path(prefs_path(env)));
    const auto restored = session->get();
    ui.session = session;
    ui.theme = builtin_theme(!theme.empty() ? theme : !restored.theme.empty() ? std::string_view{restored.theme} : std::string_view{"dark"});
    ui.caps = apply(detect_caps(env), prefs ? *prefs : TerminalPrefs{});
    ui.terminal_size = [] {
        const auto d = ftxui::Terminal::Size();
        return std::pair{d.dimx, d.dimy};
    };
    ui.user_filters = user_filters_dir(env);
    ui.repo_root = SIGNALS_SOURCE_DIR;
    ui.editor = prefs && prefs->editor ? *prefs->editor : std::string{};
    ui.open_in_editor = [&app, &ui, env](std::filesystem::path path) {
        app.WithRestoredIO([argv = editor_command(path, env, ui.editor)] { launch(argv); })();
    };
    ui.copy_text = [](std::string text) {
        std::fputs(osc52(text).c_str(), stdout);
        std::fflush(stdout);
    };
    const auto library = load_user_banks(ui.user_filters);
    set_user_banks(library.banks);
    install_user_wavelets();
    set_user_filters(library.filters);
    auto models = make_app_models();
    models.learn = std::make_shared<LearnState>(learn::builtin_course(), prefs_path(env).parent_path(), models.pipeline);
    if (const auto warning = models.learn->view().warning) models.signal->add_log(*warning, LogLine::warn);
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
    ui.first_run_start = !file && session->fresh();
    ui.warn = [signal = models.signal](std::string line) { signal->add_log(std::move(line), LogLine::warn); };
    ui.open_file = [signal = models.signal, &jobs](const std::filesystem::path& path) { signal->open(path, jobs); };
    models.signal->set_on_loaded([session](const std::filesystem::path& path) { session->remember(RecentKind::audio, path); });
    ui.open_graph = [&ui, pipeline = models.pipeline](const std::filesystem::path& path) {
        if (auto ok = pipeline->load(path); !ok) {
            if (ui.notice) ui.notice(ok.error().message);
            return;
        }
        if (ui.session) ui.session->remember(RecentKind::graph, path);
        if (ui.navigate) ui.navigate("tab:8", {});
    };
    bool relaunch = false;
    ui.switch_language = [&](Language next) {
        auto saved = load_prefs(ui.prefs_file).value_or(TerminalPrefs{});
        saved.language = next;
        (void)save_prefs(ui.prefs_file, saved);
        set_language(next);
        relaunch = true;
        app.Exit();
    };
    int tab = 0;
    if (file) {
        models.signal->open(*file, jobs);
    } else {
        if (const auto audio = session->recent(RecentKind::audio); !audio.empty()) models.signal->open(audio.front(), jobs);
        if (const auto graphs = session->recent(RecentKind::graph); !graphs.empty()) (void)models.pipeline->load(graphs.front());
        tab = std::clamp(restored.tab, 0, 8);
    }
    do {
        relaunch = false;
        auto shell = make_shell(ui, make_app_screens(ui, models), [&app] { app.Exit(); });
        if (tab > 0 && ui.navigate) ui.navigate("tab:" + std::to_string(tab + 1), {});
        app.Loop(shell);
        tab = active_tab(shell);
    } while (relaunch);
    session->set_tab(tab);
    return 0;
}

}  // namespace signals::tui
