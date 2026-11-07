#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/string.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>

#include "audio/manual_sink.hpp"
#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/tempo_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::tr;
using signals::test::Harness;

namespace {

struct Fixture {
    std::shared_ptr<UiContext> ui;
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<TempoScreen> screen;

    explicit Fixture(int w = 150, int h = 44) : ui{signals::test::make_ui(w, h)} {
        ui->jobs = &jobs;
        screen = std::make_shared<TempoScreen>(*ui, signal);
        screen->set_breakpoint(classify(w, h));
    }
};

int widest(const std::string& frame) {
    std::istringstream in{frame};
    int w = 0;
    for (std::string line; std::getline(in, line);) w = std::max(w, ftxui::string_width(line));
    return w;
}

}  // namespace

TEST_CASE("the tempo view stretches without moving the pitch and shifts by an octave", "[tui][tempo]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    auto frame = h.frame();
    CHECK(frame.find(tr("tui.tempo.title")) != std::string::npos);
    REQUIRE(f.screen->view());
    h.send(ftxui::Event::Character('s'));
    h.send(ftxui::Event::Character('s'));
    REQUIRE(f.screen->options().source == TempoSource::tone);
    h.frame();
    auto v = f.screen->view();
    REQUIRE(v);
    CHECK(std::abs(v->seconds_out - v->seconds_in / 1.25) < 1e-3);
    CHECK(std::abs(cents_off(v->f0_out_median, v->f0_in_median)) < 2);
    CHECK(std::abs(cents_off(v->f0_in_median, kTempoToneHz)) < 5);
    h.send(ftxui::Event::Character('m'));
    CHECK(f.screen->options().mode == TempoMode::shift);
    h.frame();
    v = f.screen->view();
    REQUIRE(v);
    CHECK(std::abs(v->seconds_out - v->seconds_in) < 1e-3);
    CHECK(std::abs(cents_off(v->f0_out_median, 2 * v->f0_in_median)) < 2);
    CHECK(std::abs(cents_off(v->f0_out_median, v->expected)) < 2);
    h.send(ftxui::Event::Character('m'));
    h.send(ftxui::Event::Character('+'));
    h.send(ftxui::Event::Character('+'));
    CHECK(f.screen->options().mode == TempoMode::speed);
    CHECK(f.screen->options().rate == 2.0);
    h.frame();
    v = f.screen->view();
    REQUIRE(v);
    CHECK(std::abs(v->seconds_out - v->seconds_in / 2) < 1e-3);
    CHECK(std::abs(cents_off(v->f0_out_median, 2 * v->f0_in_median)) < 2);
    frame = h.frame();
    CHECK(frame.find(tr("tui.tempo.law.speed").substr(0, 20)) != std::string::npos);
}

TEST_CASE("tempo keys step the rate, the semitones, the method and the source", "[tui][tempo]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('-'));
    CHECK(f.screen->options().rate == 1.1);
    for (int k = 0; k < 20; ++k) h.send(ftxui::Event::Character('-'));
    CHECK(f.screen->options().rate == kTempoRates.front());
    for (int k = 0; k < 40; ++k) h.send(ftxui::Event::Character('>'));
    CHECK(f.screen->options().semitones == 24);
    h.send(ftxui::Event::Character('<'));
    CHECK(f.screen->options().semitones == 23);
    h.send(ftxui::Event::Character('w'));
    CHECK(f.screen->options().method == signals::core::StretchMethod::locked);
    h.send(ftxui::Event::Character('w'));
    CHECK(f.screen->options().method == signals::core::StretchMethod::wsola);
    h.frame();
    REQUIRE(f.screen->view());
    h.send(ftxui::Event::Character('s'));
    CHECK(f.screen->options().source == TempoSource::file);
    const auto frame = h.frame();
    CHECK(f.screen->notice() == tr("tui.tempo.no_file"));
    CHECK(frame.find(tr("tui.tempo.no_file").substr(0, 24)) != std::string::npos);
    h.send(ftxui::Event::Character('s'));
    CHECK(f.screen->options().source == TempoSource::tone);
    h.frame();
    CHECK(f.screen->view());
}

TEST_CASE("the tempo view takes the selected fragment of the open file", "[tui][tempo]") {
    Fixture f;
    f.signal->open(signals::test::sample("test.wav"), f.jobs);
    Harness h{f.screen, 150, 44};
    h.send(ftxui::Event::Character('s'));
    REQUIRE(f.screen->options().source == TempoSource::file);
    h.frame();
    const auto v = f.screen->view();
    REQUIRE(v);
    CHECK(v->fs == f.signal->data()->info.sample_rate);
    CHECK(v->seconds_in <= kTempoSeconds + 1e-9);
    CHECK(std::abs(v->seconds_out - v->seconds_in / 1.25) < 1e-3);
}

TEST_CASE("a and b play the original and the processed fragment", "[tui][tempo][audio]") {
    Fixture f;
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    f.ui->player = &player;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('a'));
    CHECK(player.tag() == "tempo.a");
    h.send(ftxui::Event::Character('b'));
    CHECK(player.tag() == "tempo.b");
    h.send(ftxui::Event::Character(' '));
    CHECK(player.state() != signals::audio::PlayState::playing);
    h.frame();
    const auto b = f.screen->area("play_a");
    REQUIRE(b.x_max >= b.x_min);
    h.click(b.x_min, b.y_min);
    CHECK(player.tag() == "tempo.a");
}

TEST_CASE("the tempo view fits 80×24 and shows every control", "[tui][tempo][fit]") {
    Fixture f{80, 24};
    Harness h{f.screen, 80, 24};
    const auto frame = h.frame();
    CHECK(widest(frame) <= 80);
    for (const char* key : {"tui.tempo.mode.speed", "tui.tempo.method.wsola", "tui.tempo.source.file", "tui.tempo.play.b"}) {
        INFO(key);
        CHECK(frame.find(tr(key)) != std::string::npos);
    }
}

TEST_CASE("tempo panels take focus with Tab, z maximizes, the wheel zooms and 0 resets", "[tui][tempo][panels]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Tab);
    CHECK(f.screen->focus() == TempoScreen::before_panel);
    h.send(ftxui::Event::Character('z'));
    auto frame = h.frame();
    CHECK(frame.find(tr("tui.tempo.after")) == std::string::npos);
    h.send(ftxui::Event::Escape);
    h.frame();
    const auto b = f.screen->area("before_plot");
    REQUIRE(b.x_max > b.x_min);
    const auto full = f.screen->plot_view("before");
    REQUIRE(full);
    const int x = b.x_min + (b.x_max - b.x_min) / 3, y = (b.y_min + b.y_max) / 2;
    h.wheel(x, y, true);
    h.wheel(x, y, true);
    h.frame();
    CHECK(f.screen->plot_view("before")->hi - f.screen->plot_view("before")->lo < 0.7 * (full->hi - full->lo));
    h.click(x, y);
    h.send(ftxui::Event::Character('0'));
    h.frame();
    CHECK(f.screen->plot_view("before")->hi - f.screen->plot_view("before")->lo == full->hi - full->lo);
    const auto link = f.screen->theory_link();
    REQUIRE(link);
    CHECK(link->id == "time_stretch");
}

TEST_CASE("the tempo rate ladder clamps at both ends", "[tui][tempo]") {
    CHECK(next_tempo_rate(1.25, true) == 1.5);
    CHECK(next_tempo_rate(1.25, false) == 1.1);
    CHECK(next_tempo_rate(4.0, true) == 4.0);
    CHECK(next_tempo_rate(0.25, false) == 0.25);
    CHECK(next_tempo_rate(1.3, false) == 1.25);
}

TEST_CASE("tempo snapshots", "[tui][tempo][snapshot]") {
    for (auto [w, hgt] : {std::pair{150, 44}, std::pair{80, 24}}) {
        Fixture f{w, hgt};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, hgt};
        signals::test::check_snapshot("tempo_" + std::to_string(w) + "x" + std::to_string(hgt), shell.frame());
    }
}
