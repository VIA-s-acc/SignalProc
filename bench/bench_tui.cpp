#include "bench_tui.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <nanobench.h>

#include <sys/resource.h>

#include "audio/player.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/decomposition_screen.hpp"
#include "tui/screens/designer_screen.hpp"
#include "tui/screens/explain_screen.hpp"
#include "tui/screens/matrix_screen.hpp"
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

class NullSink final : public signals::audio::Sink {
public:
    signals::core::Result<void> start(std::uint32_t, std::uint32_t, Pull) override { return {}; }
    void stop() override {}
};

double peak_rss_mb() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
    return static_cast<double>(u.ru_maxrss) / (1 << 20);
#else
    return static_cast<double>(u.ru_maxrss) / 1024;
#endif
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

    auto designer_model = std::make_shared<DesignerModel>();
    designer_model->set_p(3, inline_jobs);
    auto designer_shell = make_shell(*ui, {std::make_shared<DesignerScreen>(*ui, designer_model)}, [] {});
    draw(designer_shell);
    b.run("frame «Конструктор», p = 3", [&] { draw(designer_shell); });

    auto explain = std::make_shared<ExplainScreen>(*ui, [] { return std::optional<signals::core::FilterBank>{}; });
    explain->open_with(NavArgs{.step = 4});
    auto explain_shell = make_shell(*ui, {explain}, [] {});
    draw(explain_shell);
    b.run("frame «Объяснение», step 4", [&] { draw(explain_shell); });

    signals::audio::Player player{std::unique_ptr<signals::audio::Sink>{std::make_unique<NullSink>()}};
    ui->player = &player;
    auto decomposition_model = std::make_shared<DecompositionModel>(model);
    auto decomposition = std::make_shared<DecompositionScreen>(*ui, decomposition_model);
    auto decomposition_shell = make_shell(*ui, {decomposition}, [] {});
    draw(decomposition_shell);
    b.run("frame «Разложение» with test.wav", [&] { draw(decomposition_shell); });
    decomposition->set_cursor(2.0);
    decomposition_shell->OnEvent(ftxui::Event::Character('b'));
    b.run("frame «Разложение» while playing B", [&] { draw(decomposition_shell); });
    player.stop();
    ui->player = nullptr;

    auto matrix_model = std::make_shared<MatrixModel>();
    auto matrix_shell = make_shell(*ui, {std::make_shared<MatrixScreen>(*ui, matrix_model)}, [] {});
    draw(matrix_shell);
    b.run("frame «Матрица», db2 N = 32", [&] { draw(matrix_shell); });

    ui->caps.color = ColorDepth::palette256;
    draw(signal_shell);
    b.run("frame «Сигнал», 256 colours", [&] { draw(signal_shell); });
    draw(filters_shell);
    b.run("frame «Фильтры», 256 colours", [&] { draw(filters_shell); });
    for (auto* shell : {&designer_shell, &explain_shell, &decomposition_shell, &matrix_shell}) draw(*shell);
    b.run("frame «Конструктор», 256 colours", [&] { draw(designer_shell); });
    b.run("frame «Объяснение», 256 colours", [&] { draw(explain_shell); });
    b.run("frame «Разложение», 256 colours", [&] { draw(decomposition_shell); });
    b.run("frame «Матрица», 256 colours", [&] { draw(matrix_shell); });
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

    auto big_decomposition = std::make_shared<DecompositionModel>(big);
    const auto d0 = std::chrono::steady_clock::now();
    big_decomposition->sync(jobs);
    while (!big_decomposition->recon()) std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const std::chrono::duration<double, std::milli> decomposed = std::chrono::steady_clock::now() - d0;
    std::printf("\ndecomposition of 10 min stereo (mono, db2, J = 6, MRA): %.0f ms, peak RSS so far %.0f MB\n", decomposed.count(), peak_rss_mb());
    for (const auto& v : jobs.snapshot())
        if (v.title.find("Разложение") != std::string::npos)
            for (const auto& s : v.stages) std::printf("  %s %8.1f ms\n", pad_right(s.name, 24).c_str(), s.seconds * 1000);
    auto big_designer = std::make_shared<DesignerModel>();
    const auto p0 = std::chrono::steady_clock::now();
    big_designer->set_p(8, jobs);
    while (!big_designer->data()) std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const std::chrono::duration<double, std::milli> splits = std::chrono::steady_clock::now() - p0;
    std::printf("designer p = 8: %zu splits classified in %.0f ms\n", big_designer->data()->rows.size(), splits.count());
}
