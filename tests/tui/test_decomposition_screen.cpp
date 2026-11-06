#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/decomposition_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

std::string line_with(const std::string& frame, const std::string& needle) {
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);)
        if (line.find(needle) != std::string::npos) return line;
    return {};
}

struct DecompositionFixture {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<DecompositionModel> model;
    std::shared_ptr<DecompositionScreen> screen;
    Harness h;
    std::string nav;

    explicit DecompositionFixture(bool load = true, int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)},
          model{(ui->jobs = &jobs, ui->player = &player, load ? (signal->open(signals::test::sample("test.wav"), jobs), 0) : 0,
                 std::make_shared<DecompositionModel>(signal))},
          screen{std::make_shared<DecompositionScreen>(*ui, model)},
          h{screen, w, hgt - 3} {
        ui->navigate = [this](std::string_view t, const NavArgs&) { nav = std::string{t}; };
        screen->set_breakpoint(classify(w, hgt));
        h.frame();
    }
    void click(std::string_view id) {
        const auto b = screen->area(id);
        REQUIRE(b.x_max >= b.x_min);
        h.click(b.x_min, b.y_min);
    }
};

}  // namespace

TEST_CASE("subbands list the canvas bands for test.wav", "[tui][decomposition]") {
    DecompositionFixture f;
    const auto frame = f.h.frame();
    for (const char* s : {"☑ d₁", "0,79 %", "225 344", "☑ a₆", "65,55 %", "7 044", "PR (все ☑)", "все полосы включены: x̂ = x", "Подполосы"})
        CHECK(frame.find(s) != std::string::npos);
    CHECK(f.screen->context().find("test.wav") != std::string::npos);
}

TEST_CASE("clicking a checkbox removes a band and warns about aliasing", "[tui][decomposition]") {
    DecompositionFixture f;
    f.click("check_0");
    const auto frame = f.h.frame();
    CHECK(frame.find("☐ d₁") != std::string::npos);
    CHECK(frame.find("выключено полос: 1") != std::string::npos);
    CHECK(line_with(frame, "энергия").find("99,2") != std::string::npos);
}

TEST_CASE("arrows and enter toggle the selected band", "[tui][decomposition]") {
    DecompositionFixture f;
    CHECK(f.screen->selected_band() == 0);
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowDown);
    CHECK(f.screen->selected_band() == 2);
    f.h.send(ftxui::Event::Return);
    CHECK_FALSE(f.model->enabled()[2]);
    CHECK(f.h.frame().find("☐ d₃") != std::string::npos);
}

TEST_CASE("v cycles subbands, MRA and tiles", "[tui][decomposition]") {
    DecompositionFixture f;
    CHECK(f.screen->view() == DecompositionScreen::View::subbands);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::mra);
    CHECK(f.h.frame().find("MRA · x = d₁ + … + a₆") != std::string::npos);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::tiles);
    CHECK(f.h.frame().find("Плитки t–f") != std::string::npos);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::subbands);
}

TEST_CASE("enter in the denoise panel applies and u undoes", "[tui][decomposition]") {
    DecompositionFixture f;
    f.click("denoise_panel");
    f.h.send(ftxui::Event::Return);
    REQUIRE(f.model->recon());
    CHECK(f.model->recon()->processing.rule);
    CHECK(f.model->recon()->energy_kept_pct < 100);
    f.h.send(ftxui::Event::Character('u'));
    CHECK_FALSE(f.model->recon()->processing.rule);
}

TEST_CASE("b plays the enabled bands from the cursor and a and x switch in place", "[tui][decomposition][audio]") {
    DecompositionFixture f;
    f.screen->set_cursor(2.0);
    f.h.send(ftxui::Event::Character('b'));
    CHECK(f.player.tag() == "decomposition.enabled");
    CHECK(f.player.position() == 88'200);
    f.raw->pump(100);
    f.h.send(ftxui::Event::Character('a'));
    CHECK(f.player.tag() == "decomposition.original");
    CHECK(f.player.position() == 88'300);
    f.h.send(ftxui::Event::Character('x'));
    CHECK(f.player.tag() == "decomposition.difference");
    f.h.send(ftxui::Event::Character(' '));
    CHECK(f.player.state() == signals::audio::PlayState::stopped);
}

TEST_CASE("M6 chips explain when they arrive", "[tui][decomposition]") {
    DecompositionFixture f;
    f.click("chip_swt");
    CHECK(f.h.frame().find("появится в M6") != std::string::npos);
}

TEST_CASE("wheel zooms time and a click sets the cursor", "[tui][decomposition]") {
    DecompositionFixture f;
    const auto a = f.screen->area("view_plot");
    REQUIRE(a.x_max > a.x_min);
    const int x = (a.x_min + a.x_max) / 2, y = (a.y_min + a.y_max) / 2;
    const auto [b0, e0] = f.screen->time_view();
    f.h.wheel(x, y, true);
    const auto [b1, e1] = f.screen->time_view();
    CHECK(e1 - b1 < e0 - b0);
    f.h.click(x, y);
    REQUIRE(f.screen->cursor());
    CHECK(*f.screen->cursor() > b1);
    CHECK(*f.screen->cursor() < e1);
}

TEST_CASE("decomposition snapshots", "[tui][decomposition][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        DecompositionFixture f{true, w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("decomposition_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("round-off-level PR errors print the same on every compiler", "[tui][decomposition]") {
    DecompositionFixture f;
    CHECK(line_with(f.h.frame(), "PR (все ☑)").find("< 10⁻¹⁴") != std::string::npos);
}

TEST_CASE("escape cancels a running decomposition", "[tui][decomposition]") {
    JobRunner inline_jobs{[] {}, 0ms, true};
    JobRunner jobs{[] {}};
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    auto signal = std::make_shared<SignalModel>();
    signal->open(signals::test::sample("test.wav"), inline_jobs);
    auto model = std::make_shared<DecompositionModel>(signal);
    auto screen = std::make_shared<DecompositionScreen>(*ui, model);
    Harness h{screen, 150, 41};
    h.frame();
    REQUIRE(model->job());
    CHECK(h.send(ftxui::Event::Escape));
    CHECK_FALSE(model->job());
    const auto until = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < until && jobs.queued() > 0) std::this_thread::sleep_for(10ms);
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(model->data());
    CHECK(h.frame().find("прервано") != std::string::npos);
}

TEST_CASE("o on an empty decomposition asks for a file", "[tui][decomposition]") {
    DecompositionFixture f{false};
    int asked = 0;
    f.ui->ask_open = [&] { ++asked; };
    CHECK(f.h.frame().find("o — открыть WAV, : — команды") != std::string::npos);
    CHECK(f.h.send(ftxui::Event::Character('o')));
    CHECK(asked == 1);
}
