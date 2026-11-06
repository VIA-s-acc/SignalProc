#include "tui/run.hpp"

#include <memory>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include "i18n/i18n.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/screens/filters_model.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/screens/designer_screen.hpp"
#include "tui/screens/explain_screen.hpp"
#include "tui/screens/filters_screen.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/screens/signal_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/shell/subtabs.hpp"
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
    ui.user_filters = user_filters_dir(env);
    const auto library = load_user_banks(ui.user_filters);
    set_user_banks(library.banks);
    auto signal = std::make_shared<SignalModel>();
    for (const auto& e : library.errors)
        signal->add_log(fmt::format(fmt::runtime(tr("tui.user_filters.broken")), fmt::arg("error", e)), LogLine::warn);
    auto designer = std::make_shared<DesignerModel>();
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
    designer->set_p(3, jobs);
    screens[3] = make_subtabs(tr("tui.tab.bank"), {{tr("tui.sub.designer"), make_designer_screen(ui, designer)},
                                                  {tr("tui.sub.diagram"), make_placeholder(tr("tui.sub.diagram"), tr("tui.placeholder.diagram.title"),
                                                                                           tr("tui.placeholder.diagram.text"), "M6", &ui)}});
    screens[8] = make_subtabs(tr("tui.tab.theory"),
                              {{tr("tui.sub.explain"), make_explain_screen(ui, [] {
                                   const auto banks = user_banks();
                                   if (banks.empty()) return std::optional<core::FilterBank>{};
                                   auto bank = core::make_bank<double>(banks.back().name, banks.back().h0, banks.back().f0, 1e-9);
                                   return bank ? std::optional<core::FilterBank>{std::move(*bank)} : std::nullopt;
                               })},
                               {tr("tui.sub.under_the_hood"), make_placeholder(tr("tui.sub.under_the_hood"), tr("tui.placeholder.under_the_hood.title"),
                                                                               tr("tui.placeholder.under_the_hood.text"), "M4b", &ui)},
                               {tr("tui.sub.tutorial"), make_placeholder(tr("tui.sub.tutorial"), tr("tui.placeholder.tutorial.title"),
                                                                         tr("tui.placeholder.tutorial.text"), "M4b", &ui)}});
    if (file) signal->open(*file, jobs);
    auto shell = make_shell(ui, std::move(screens), [&app] { app.Exit(); });
    app.Loop(shell);
    return 0;
}

}  // namespace signals::tui
