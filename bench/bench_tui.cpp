#include "bench_tui.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <nanobench.h>

#include "tui/jobs/jobs.hpp"
#include "tui/screens/filters_screen.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/screens/signal_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/widgets/text_util.hpp"

using namespace signals::tui;

namespace {

constexpr int kCols = 150, kRows = 44;

std::unique_ptr<UiContext> make_ui() {
    auto ui = std::make_unique<UiContext>();
    ui->theme = builtin_theme("dark");
    ui->caps = {ColorDepth::truecolor, GlyphMode::unicode, "bench"};
    ui->terminal_size = [] { return std::pair{kCols, kRows}; };
    ui->resources = [] {
        ResourceSnapshot s;
        s.core_load_pct.assign(8, 10.0);
        return s;
    };
    return ui;
}

void draw(const ftxui::Component& root) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(kCols), ftxui::Dimension::Fixed(kRows));
    ftxui::Render(screen, root->Render());
    ankerl::nanobench::doNotOptimizeAway(screen.CellAt(1, 1).character);
}

ftxui::Event wheel(int x, int y, bool up) {
    ftxui::Mouse m;
    m.button = up ? ftxui::Mouse::WheelUp : ftxui::Mouse::WheelDown;
    m.motion = ftxui::Mouse::Pressed;
    m.x = x;
    m.y = y;
    return ftxui::Event::Mouse("", m);
}

}  // namespace

void bench_tui(const std::filesystem::path& ten_minutes) {
    ankerl::nanobench::Bench b;
    b.title("TUI 150x44, release").minEpochIterations(5);

    auto ui = make_ui();
    JobRunner inline_jobs{[] {}, std::chrono::milliseconds{0}, true};
    ui->jobs = &inline_jobs;
    auto model = std::make_shared<SignalModel>();
    model->open(std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav", inline_jobs);
    auto signal = std::make_shared<SignalScreen>(*ui, model);
    auto signal_shell = make_shell(*ui, {signal}, [] {});
    draw(signal_shell);
    b.run("frame «Сигнал» with test.wav", [&] { draw(signal_shell); });

    const auto wave = signal->area("waveform");
    const int x = (wave.x_min + wave.x_max) / 2, y = (wave.y_min + wave.y_max) / 2;
    bool up = true;
    b.run("wheel over the waveform + frame", [&] {
        signal_shell->OnEvent(wheel(x, y, up));
        up = !up;
        draw(signal_shell);
    });

    auto set = std::make_shared<ComparisonSet>();
    for (const auto& r : {FilterRef{FilterRef::Kind::bank, "db2", FilterRole::h0}, FilterRef{FilterRef::Kind::bank, "db2", FilterRole::h1},
                          FilterRef{FilterRef::Kind::bank, "db2", FilterRole::f0},
                          FilterRef{FilterRef::Kind::equivalent, "db2", FilterRole::h0, 2, true}})
        (void)set->add(r);
    auto filters = std::make_shared<FiltersScreen>(*ui, set);
    filters->set_cursor_a(0.5);
    auto filters_shell = make_shell(*ui, {filters}, [] {});
    draw(filters_shell);
    b.run("frame «Фильтры» with four traces", [&] { draw(filters_shell); });

    ui->caps.color = ColorDepth::palette256;
    draw(signal_shell);
    b.run("frame «Сигнал», 256 colours", [&] { draw(signal_shell); });
    draw(filters_shell);
    b.run("frame «Фильтры», 256 colours", [&] { draw(filters_shell); });
    ui->caps.color = ColorDepth::truecolor;

    auto threaded_ui = make_ui();
    JobRunner jobs{[] {}};
    threaded_ui->jobs = &jobs;
    auto big = std::make_shared<SignalModel>();
    auto big_shell = make_shell(*threaded_ui, {std::make_shared<SignalScreen>(*threaded_ui, big)}, [] {});
    draw(big_shell);
    const auto t0 = std::chrono::steady_clock::now();
    big->open(ten_minutes, jobs);
    draw(big_shell);
    const std::chrono::duration<double, std::milli> first = std::chrono::steady_clock::now() - t0;
    while (!big->data()) {
        draw(big_shell);
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    const std::chrono::duration<double, std::milli> loaded = std::chrono::steady_clock::now() - t0;
    std::vector<std::pair<std::string, double>> stages;
    for (const auto& v : jobs.snapshot())
        for (const auto& s : v.stages) stages.emplace_back(s.name, s.seconds * 1000);
    std::printf("\nopen 10 min stereo: first frame with the job after %.1f ms, data published after %.0f ms\n", first.count(), loaded.count());
    for (const auto& [name, ms] : stages) std::printf("  %s %8.1f ms\n", pad_right(name, 24).c_str(), ms);
    draw(big_shell);
    b.run("frame «Сигнал» with 10 min stereo", [&] { draw(big_shell); });
}
