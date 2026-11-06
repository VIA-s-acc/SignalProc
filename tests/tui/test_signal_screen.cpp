#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <regex>
#include <string>
#include <thread>

#include "core/metrics.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/screens/signal_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<SignalModel> loaded(JobRunner& jobs) {
    auto m = std::make_shared<SignalModel>();
    m->open(signals::test::sample("test.wav"), jobs);
    return m;
}

std::string normalise(std::string text) {
    text = std::regex_replace(text, std::regex{R"(\d\d:\d\d:\d\d)"}, "hh:mm:ss");
    return std::regex_replace(text, std::regex{R"( *\d+(,\d+)? (с|мс|мкс))"}, " <t>");
}

double fraction_of(const SignalModel& m, double t) { return (t - m.view_begin()) / (m.view_end() - m.view_begin()); }

}  // namespace

TEST_CASE("signal model loads test.wav in named stages", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto m = loaded(jobs);
    auto d = m->data();
    REQUIRE(d);
    CHECK(d->info.frames == 450'686);
    CHECK(d->stats[0].clipped == 1797);
    CHECK(d->samples.size() == 2);
    CHECK(d->pyramids.size() == 2);
    const auto job = jobs.snapshot().back();
    REQUIRE(job.stages.size() == 5);
    for (const auto& s : job.stages) CHECK(s.state == StageState::done);
    CHECK(std::abs(signals::core::neumaier_sum(d->band_energy) - 1.0) < 1e-12);
    CHECK(d->band_energy.size() == 7);
    CHECK(d->psd_db.size() == d->psd_hz.size());
    CHECK(*std::max_element(d->psd_db.begin(), d->psd_db.end()) == 0.0);
    CHECK(d->memory.bytes() > 0);
    CHECK(m->view_begin() == 0.0);
    CHECK(m->view_end() == Catch::Approx(450'686.0 / 44'100));
}

TEST_CASE("zoom keeps the time under the mouse", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto m = loaded(jobs);
    const double before = fraction_of(*m, 3.0);
    m->zoom(2, 3.0);
    CHECK(fraction_of(*m, 3.0) == Catch::Approx(before).margin(1e-9));
    m->zoom(2, 3.0);
    CHECK(fraction_of(*m, 3.0) == Catch::Approx(before).margin(1e-9));
    m->zoom(1e9, 3.0);
    CHECK((m->view_end() - m->view_begin()) * 44'100 == Catch::Approx(32).margin(1e-6));
    m->zoom(1e-9, 3.0);
    CHECK(m->view_begin() == 0.0);
    CHECK(m->view_end() == Catch::Approx(450'686.0 / 44'100));
    m->zoom_amplitude(1e6);
    CHECK(m->amplitude() == std::ldexp(1.0, -10));
    m->zoom_amplitude(1e-6);
    CHECK(m->amplitude() == 1.0);
}

TEST_CASE("pan, cursors and selections stay inside the signal", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto m = loaded(jobs);
    const double duration = m->view_end();
    m->zoom(4, 5.0);
    m->pan(-100);
    CHECK(m->view_begin() == 0.0);
    m->pan(100);
    CHECK(m->view_end() == Catch::Approx(duration));
    m->set_cursor_a(-1);
    CHECK(m->cursor_a() == 0.0);
    m->set_cursor_b(1e6);
    CHECK(m->cursor_b() == Catch::Approx(duration));
    m->select(4, 2);
    REQUIRE(m->selection());
    CHECK(m->selection()->first == 2.0);
    CHECK(m->selection()->second == 4.0);
}

TEST_CASE("selection statistics come from a job", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto m = loaded(jobs);
    m->select(2.10, 4.85);
    m->compute_selection(jobs);
    auto s = m->selection_stats();
    REQUIRE(s.size() == 2);
    CHECK(s[0].duration == Catch::Approx(2.75).margin(1e-3));
    CHECK(s[0].peak_dbfs <= 0.0);
    CHECK(s[0].rms_dbfs < s[0].peak_dbfs);
}

TEST_CASE("broken, empty and cancelled loads keep the screen alive", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    SignalModel m;
    m.open(signals::test::fixture("truncated.wav"), jobs);
    CHECK_FALSE(m.data());
    REQUIRE_FALSE(m.log().empty());
    CHECK(m.log().back().level == LogLine::error);
    CHECK(m.log().back().text.find("Ошибка") != std::string::npos);
    m.open(signals::test::fixture("empty.wav"), jobs);
    CHECK_FALSE(m.data());
    CHECK(m.log().back().level == LogLine::error);
    JobRunner threaded{[] {}};
    SignalModel c;
    c.open(signals::test::sample("test.wav"), threaded);
    REQUIRE(c.job());
    threaded.cancel(*c.job());
    for (int k = 0; k < 2000; ++k) {
        bool done = false;
        for (const auto& v : threaded.snapshot()) done = done || (v.id == *c.job() && v.finished);
        if (done) break;
        std::this_thread::sleep_for(1ms);
    }
    const auto v = threaded.snapshot().back();
    if (v.cancelled) CHECK_FALSE(c.data());
}

TEST_CASE("mouse on the waveform zooms, pans, selects and sets cursors", "[tui][signal]") {
    auto ui = signals::test::make_ui(150, 44);
    JobRunner jobs{[] {}, 0ms, true};
    ui->jobs = &jobs;
    auto model = loaded(jobs);
    auto screen = std::make_shared<SignalScreen>(*ui, model);
    screen->set_breakpoint(Breakpoint::primary);
    Harness h{screen, 120, 40};
    h.frame();
    const auto wave = screen->area("waveform");
    REQUIRE(wave.x_max > wave.x_min);
    const int x = (wave.x_min + wave.x_max) / 2, y = (wave.y_min + wave.y_max) / 2;
    const double span = model->view_end() - model->view_begin();
    h.wheel(x, y, true);
    CHECK(model->view_end() - model->view_begin() == Catch::Approx(span / 1.25));
    h.frame();
    const double span2 = model->view_end() - model->view_begin();
    h.wheel(x, y, true, true);
    CHECK(model->amplitude() < 1.0);
    CHECK(model->view_end() - model->view_begin() == Catch::Approx(span2));
    const double begin = model->view_begin();
    h.drag(x, y, x - 10, y);
    CHECK(model->view_begin() > begin);
    h.frame();
    h.drag(x - 5, y, x + 5, y, true);
    REQUIRE(model->selection());
    CHECK(model->selection()->second > model->selection()->first);
    h.frame();
    h.click(x, y);
    REQUIRE(model->cursor_a());
    const double a = *model->cursor_a();
    CHECK(a > model->view_begin());
    CHECK(a < model->view_end());
    const auto log = screen->area("log");
    const double before = model->view_end() - model->view_begin();
    h.wheel((log.x_min + log.x_max) / 2, (log.y_min + log.y_max) / 2, true);
    CHECK(model->view_end() - model->view_begin() == Catch::Approx(before));
    CHECK(h.frame().find("nan") == std::string::npos);
    CHECK(h.frame().find("—") != std::string::npos);
}

TEST_CASE("an empty signal screen invites to open a file", "[tui][signal]") {
    auto ui = signals::test::make_ui(150, 44);
    auto screen = std::make_shared<SignalScreen>(*ui, std::make_shared<SignalModel>());
    screen->set_breakpoint(Breakpoint::primary);
    auto t = Harness{screen, 150, 41}.frame();
    CHECK(t.find("открыть WAV") != std::string::npos);
}

TEST_CASE("signal screen snapshots", "[tui][signal][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        auto ui = signals::test::make_ui(w, h);
        JobRunner jobs{[] {}, 0ms, true};
        ui->jobs = &jobs;
        auto model = loaded(jobs);
        std::vector<std::shared_ptr<TabScreen>> screens{std::make_shared<SignalScreen>(*ui, model)};
        Harness shell{make_shell(*ui, screens, [] {}), w, h};
        signals::test::check_snapshot("signal_" + std::to_string(w) + "x" + std::to_string(h), normalise(shell.frame()));
    }
}

TEST_CASE("keys switch panels, maximise and move cursor A", "[tui][signal]") {
    auto ui = signals::test::make_ui(150, 44);
    JobRunner jobs{[] {}, 0ms, true};
    ui->jobs = &jobs;
    auto model = loaded(jobs);
    auto screen = std::make_shared<SignalScreen>(*ui, model);
    screen->set_breakpoint(Breakpoint::primary);
    Harness h{screen, 150, 41};
    CHECK(h.frame().find("Источник") != std::string::npos);
    h.send(ftxui::Event::Character('z'));
    auto maximised = h.frame();
    CHECK(maximised.find("Источник") == std::string::npos);
    CHECK(maximised.find("Осциллограмма") != std::string::npos);
    h.send(ftxui::Event::Escape);
    CHECK(h.frame().find("Источник") != std::string::npos);
    h.send(ftxui::Event::ArrowRight);
    REQUIRE(model->cursor_a());
    const double a = *model->cursor_a();
    h.send(ftxui::Event::ArrowRight);
    CHECK(*model->cursor_a() > a);
    h.send(ftxui::Event::Home);
    CHECK(*model->cursor_a() == 0.0);
    h.send(ftxui::Event::End);
    CHECK(*model->cursor_a() == Catch::Approx(model->data()->duration()));
    CHECK(model->view_end() == Catch::Approx(model->data()->duration()));

    screen->set_breakpoint(Breakpoint::small);
    Harness small{screen, 80, 21};
    CHECK(small.frame().find("Осциллограмма") != std::string::npos);
    small.send(ftxui::Event::Tab);
    auto next = small.frame();
    CHECK(next.find("Спектр") != std::string::npos);
    CHECK(next.find("Осциллограмма") == std::string::npos);
}

TEST_CASE("signal screen in ascii mode draws no unicode graphics", "[tui][signal]") {
    auto ui = signals::test::make_ui(150, 44, GlyphMode::ascii, ColorDepth::none);
    JobRunner jobs{[] {}, 0ms, true};
    ui->jobs = &jobs;
    auto model = loaded(jobs);
    model->set_cursor_a(3.0);
    auto screen = std::make_shared<SignalScreen>(*ui, model);
    screen->set_breakpoint(Breakpoint::primary);
    const auto t = Harness{screen, 150, 41}.frame();
    for (const char* g : {"⣿", "█", "▍", "⚠", "₆", "╭", "│", "✓"}) {
        INFO(g);
        CHECK(t.find(g) == std::string::npos);
    }
    CHECK(t.find("a6") != std::string::npos);
}

TEST_CASE("a cancelled load reads as interrupted without raw placeholders", "[tui][signal]") {
    auto ui = signals::test::make_ui(150, 44);
    std::atomic<bool> release{false}, started{false};
    JobRunner jobs{[] {}};
    ui->jobs = &jobs;
    jobs.submit("blocker", {"wait"}, [&](JobContext&) {
        started = true;
        while (!release) std::this_thread::sleep_for(1ms);
    });
    while (!started) std::this_thread::sleep_for(1ms);
    auto model = std::make_shared<SignalModel>();
    model->open(signals::test::sample("test.wav"), jobs);
    auto screen = std::make_shared<SignalScreen>(*ui, model);
    screen->set_breakpoint(Breakpoint::primary);
    Harness h{screen, 150, 41};
    h.send(ftxui::Event::Escape);
    release = true;
    for (int k = 0; k < 2000; ++k) {
        bool done = false;
        for (const auto& v : jobs.snapshot()) done = done || (v.id == *model->job() && v.finished);
        if (done) break;
        std::this_thread::sleep_for(1ms);
    }
    const auto t = h.frame();
    CHECK(t.find("задача прервана") != std::string::npos);
    CHECK(t.find("{") == std::string::npos);
    CHECK(t.find("✓ Чтение WAV") == std::string::npos);
}
