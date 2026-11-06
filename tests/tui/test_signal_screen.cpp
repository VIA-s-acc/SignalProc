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

#include "audio/manual_sink.hpp"
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

TEST_CASE("a file too large for memory is refused with a journal entry", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    SignalModel m{{}, std::size_t{1} << 20};
    m.open(signals::test::sample("test.wav"), jobs);
    CHECK_FALSE(m.data());
    REQUIRE_FALSE(m.log().empty());
    CHECK(m.log().back().level == LogLine::error);
    CHECK(m.log().back().text.find("слишком большой") != std::string::npos);
    CHECK(jobs.snapshot().back().error);
}

TEST_CASE("an empty selection has no level, not full scale", "[tui][signal]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto m = loaded(jobs);
    m->select(2.0, 2.0);
    m->compute_selection(jobs);
    const auto s = m->selection_stats();
    REQUIRE(s.size() == 2);
    CHECK(std::isnan(s[0].peak_dbfs));
    CHECK(std::isnan(s[0].rms_dbfs));
    CHECK(std::isnan(s[0].dc));
}

namespace {

struct DragRig {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui = signals::test::make_ui(150, 44);
    std::shared_ptr<SignalModel> m;
    std::shared_ptr<SignalScreen> screen;
    Harness h;
    DragRig() : m{(ui->jobs = &jobs, loaded(jobs))}, screen{std::make_shared<SignalScreen>(*ui, m)}, h{screen, 150, 41} { h.frame(); }
};

}  // namespace

TEST_CASE("select mode drags a selection without modifiers", "[tui][signal][menu]") {
    DragRig r;
    r.h.send(ftxui::Event::Character('s'));
    CHECK(r.h.frame().find("[тащить: выделение]") != std::string::npos);
    const auto a = r.screen->area("waveform");
    const int y = (a.y_min + a.y_max) / 2;
    r.h.drag(a.x_min + 10, y, a.x_min + 40, y);
    REQUIRE(r.m->selection());
    CHECK(r.m->selection()->first < r.m->selection()->second);
    r.h.send(ftxui::Event::Character('s'));
    CHECK(r.h.frame().find("тащить: сдвиг") != std::string::npos);
}

TEST_CASE("ctrl drag still selects in pan mode", "[tui][signal][menu]") {
    DragRig r;
    const auto a = r.screen->area("waveform");
    const int y = (a.y_min + a.y_max) / 2;
    const double b0 = r.m->view_begin();
    r.h.drag(a.x_min + 10, y, a.x_min + 40, y, true);
    REQUIRE(r.m->selection());
    CHECK(r.m->view_begin() == b0);
}

TEST_CASE("the drag mode chip toggles by click", "[tui][signal][menu]") {
    DragRig r;
    const auto chip = r.screen->area("drag_chip");
    REQUIRE(chip.x_max >= chip.x_min);
    r.h.click(chip.x_min, chip.y_min);
    CHECK(r.h.frame().find("[тащить: выделение]") != std::string::npos);
}

TEST_CASE("the waveform menu sets cursors and selects between them", "[tui][signal][menu]") {
    DragRig r;
    const auto a = r.screen->area("waveform");
    const int y = (a.y_min + a.y_max) / 2;
    auto pick = [&](int x, const std::string& label) {
        for (auto& item : r.screen->context_menu(x, y))
            if (item.label == label) {
                REQUIRE(item.enabled);
                item.action();
                return;
            }
        FAIL("no menu item " << label);
    };
    pick(a.x_min + 10, "Курсор A здесь");
    pick(a.x_min + 50, "Курсор B здесь");
    REQUIRE(r.m->cursor_a());
    REQUIRE(r.m->cursor_b());
    CHECK(*r.m->cursor_a() < *r.m->cursor_b());
    pick(a.x_min + 30, "Выделить от A до B");
    REQUIRE(r.m->selection());
    CHECK(r.m->selection()->first == Catch::Approx(*r.m->cursor_a()));
    CHECK(r.m->selection()->second == Catch::Approx(*r.m->cursor_b()));
    CHECK(r.screen->context_menu(a.x_min - 30, y).empty());
}

namespace {

struct PlayRig {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui = signals::test::make_ui(150, 44);
    std::shared_ptr<SignalModel> m;
    std::shared_ptr<SignalScreen> screen;
    Harness h;
    PlayRig() : m{(ui->jobs = &jobs, ui->player = &player, loaded(jobs))}, screen{std::make_shared<SignalScreen>(*ui, m)}, h{screen, 150, 41} {
        h.frame();
    }
};

}  // namespace

TEST_CASE("space plays from cursor A and the playhead follows", "[tui][signal][audio]") {
    PlayRig r;
    r.m->set_cursor_a(1.0);
    r.h.send(ftxui::Event::Character(' '));
    CHECK(r.player.state() == signals::audio::PlayState::playing);
    CHECK(r.player.position() == 44'100);
    CHECK(r.raw->channels() == 2);
    r.raw->pump(4'410);
    REQUIRE(r.screen->playhead());
    CHECK(*r.screen->playhead() == Catch::Approx(1.1).margin(1e-6));
    CHECK(r.h.frame().find("▶ 1,10 с") != std::string::npos);
    r.h.send(ftxui::Event::Character(' '));
    CHECK(r.player.state() == signals::audio::PlayState::stopped);
    CHECK_FALSE(r.screen->playhead());
}

TEST_CASE("space with a selection plays only the selection", "[tui][signal][audio]") {
    PlayRig r;
    r.m->select(2.0, 2.5);
    r.h.send(ftxui::Event::Character(' '));
    CHECK(r.player.position() == 88'200);
    r.raw->pump(30'000);
    CHECK(r.player.position() == 110'250);
    r.player.poll();
    CHECK(r.player.state() == signals::audio::PlayState::finished);
    CHECK(r.h.frame().find("▶") == std::string::npos);
}

TEST_CASE("the view follows the playhead past its right edge", "[tui][signal][audio]") {
    PlayRig r;
    r.m->zoom(10, 0);
    const double span = r.m->view_end() - r.m->view_begin();
    r.m->set_cursor_a(r.m->view_end() - span * 0.01);
    r.h.send(ftxui::Event::Character(' '));
    r.raw->pump(static_cast<std::size_t>(span * 0.05 * 44'100));
    r.h.frame();
    CHECK(r.m->view_begin() < *r.screen->playhead());
    CHECK(*r.screen->playhead() < r.m->view_end());
}

TEST_CASE("escape stops playback before cancelling jobs", "[tui][signal][audio]") {
    PlayRig r;
    r.h.send(ftxui::Event::Character(' '));
    CHECK(r.h.send(ftxui::Event::Escape));
    CHECK(r.player.state() == signals::audio::PlayState::stopped);
}

TEST_CASE("an unavailable device logs once", "[tui][signal][audio]") {
    JobRunner jobs{[] {}, 0ms, true};
    signals::audio::Player player{std::unique_ptr<signals::audio::Sink>{std::make_unique<signals::test::FailingSink>()}};
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->player = &player;
    auto m = loaded(jobs);
    auto screen = std::make_shared<SignalScreen>(*ui, m);
    Harness h{screen, 150, 41};
    h.frame();
    const auto before = m->log().size();
    h.send(ftxui::Event::Character(' '));
    h.send(ftxui::Event::Character(' '));
    const auto log = m->log();
    REQUIRE(log.size() == before + 1);
    CHECK(log.back().text.find("Звук недоступен") != std::string::npos);
    CHECK(log.back().text.find("no output device") != std::string::npos);
}

TEST_CASE("b switches the arrow keys between cursors A and B", "[tui][signal]") {
    DragRig r;
    r.m->set_cursor_a(1.0);
    r.h.send(ftxui::Event::Character('b'));
    REQUIRE(r.m->cursor_b());
    CHECK(*r.m->cursor_b() == Catch::Approx(1.0));
    r.h.send(ftxui::Event::ArrowRight);
    CHECK(*r.m->cursor_b() > 1.0);
    CHECK(*r.m->cursor_a() == Catch::Approx(1.0));
    CHECK(r.h.frame().find("←→ B") != std::string::npos);
    r.h.send(ftxui::Event::Character('b'));
    r.h.send(ftxui::Event::ArrowLeft);
    CHECK(*r.m->cursor_a() < 1.0);
    CHECK(r.h.frame().find("←→ A") != std::string::npos);
}

TEST_CASE("the wheel zooms the spectrum in decades and the menu resets it", "[tui][signal][zoom]") {
    auto ui = signals::test::make_ui(150, 44);
    JobRunner jobs{[] {}, 0ms, true};
    ui->jobs = &jobs;
    auto model = loaded(jobs);
    auto screen = std::make_shared<SignalScreen>(*ui, model);
    screen->set_breakpoint(Breakpoint::primary);
    Harness h{screen, 150, 41};
    h.frame();
    const auto s = screen->area("spectrum_plot");
    REQUIRE(s.x_max > s.x_min);
    const int x = (s.x_min + s.x_max) / 2, y = (s.y_min + s.y_max) / 2;
    const auto before = screen->plot_axis("spectrum");
    CHECK(h.wheel(x, y, true));
    h.frame();
    const auto after = screen->plot_axis("spectrum");
    CHECK(after.lo > before.lo);
    CHECK(after.hi < before.hi);
    CHECK(h.drag(x, y, x - 10, y));
    h.frame();
    CHECK(screen->plot_axis("spectrum").lo > after.lo);
    auto items = screen->context_menu(x, y);
    REQUIRE(items.size() == 1);
    items[0].action();
    h.frame();
    CHECK(screen->plot_axis("spectrum").lo == before.lo);
}

TEST_CASE("keys zoom and pan the waveform and the spectrum and select between the cursors", "[tui][signal]") {
    DragRig r;
    const double full = r.m->view_end() - r.m->view_begin();
    CHECK(r.h.send(ftxui::Event::Character('+')));
    const double zoomed = r.m->view_end() - r.m->view_begin();
    CHECK(zoomed < full);
    const double begin = r.m->view_begin();
    r.h.send(ftxui::Event::Character('>'));
    CHECK(r.m->view_begin() > begin);
    r.h.send(ftxui::Event::Character('0'));
    CHECK(r.m->view_end() - r.m->view_begin() == Catch::Approx(full));

    r.h.send(ftxui::Event::ArrowRight);
    r.h.send(ftxui::Event::Character('b'));
    for (int i = 0; i < 20; ++i) r.h.send(ftxui::Event::ArrowRight);
    REQUIRE(r.m->cursor_a());
    REQUIRE(r.m->cursor_b());
    CHECK(r.h.send(ftxui::Event::Character('v')));
    REQUIRE(r.m->selection());
    CHECK(r.m->selection()->first == Catch::Approx(std::min(*r.m->cursor_a(), *r.m->cursor_b())));
    r.h.send(ftxui::Event::Escape);
    CHECK_FALSE(r.m->selection());

    r.h.send(ftxui::Event::Tab);
    r.h.frame();
    const auto spectrum = r.screen->plot_axis("spectrum");
    r.h.send(ftxui::Event::Character('+'));
    r.h.frame();
    const auto narrower = r.screen->plot_axis("spectrum");
    CHECK(narrower.hi - narrower.lo < spectrum.hi - spectrum.lo);
}
