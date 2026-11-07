#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <cmath>
#include <numbers>
#include <string>

#include <ftxui/component/event.hpp>

#include "io/wav.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/spectrum_screen.hpp"
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

struct SpectrumFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<SpectrumModel> model;
    std::shared_ptr<SpectrumScreen> screen;
    Harness h;

    explicit SpectrumFixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)},
          model{(ui->jobs = &jobs, signal->open(signals::test::sample("test.wav"), jobs), std::make_shared<SpectrumModel>(signal))},
          screen{std::make_shared<SpectrumScreen>(*ui, model)},
          h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        h.frame();
    }
    std::pair<int, int> point(double fx, double fy) {
        const auto b = screen->area("spectrogram");
        REQUIRE(b.x_max > b.x_min);
        return {b.x_min + static_cast<int>(fx * (b.x_max - b.x_min)), b.y_min + static_cast<int>(fy * (b.y_max - b.y_min))};
    }
};

}  // namespace

TEST_CASE("spectrum snapshots", "[tui][spectrum][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        SpectrumFixture f{w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        shell.frame();
        signals::test::check_snapshot("spectrum_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("a click on the spectrogram places the cursor and fills the readout", "[tui][spectrum]") {
    SpectrumFixture f;
    REQUIRE(f.model->spectrogram());
    const auto [x, y] = f.point(0.33, 0.5);
    f.h.click(x, y);
    const auto c = f.screen->cursor();
    REQUIRE(c);
    CHECK(c->first > 2.5);
    CHECK(c->first < 4.5);
    const auto frame = f.h.frame();
    CHECK(line_with(frame, "время").find(" с") != std::string::npos);
    CHECK(line_with(frame, "нота").find("¢") != std::string::npos);
    CHECK(line_with(frame, "уровень").find("дБ") != std::string::npos);
}

TEST_CASE("dragging measures a box and shift-dragging selects the spectrum", "[tui][spectrum]") {
    SpectrumFixture f;
    const auto [x0, y0] = f.point(0.5, 0.3);
    const auto [x1, y1] = f.point(0.6, 0.6);
    f.h.drag(x0, y0, x1, y1);
    const auto m = f.screen->measurement();
    REQUIRE(m);
    CHECK(m->t1 > m->t0);
    CHECK(m->f1 > m->f0);
    CHECK(line_with(f.h.frame(), "ср. уровень").find("дБ") != std::string::npos);
    f.h.drag(x0, y0, x1, y0, true);
    const auto sel = f.model->selection();
    REQUIRE(sel);
    CHECK(sel->t0 == m->t0);
}

TEST_CASE("w and W change the window and the resolution follows", "[tui][spectrum]") {
    SpectrumFixture f;
    CHECK(f.model->options().n == 4096);
    CHECK(line_with(f.h.frame(), "Δf = fs/N").find("10,8") != std::string::npos);
    f.h.send(ftxui::Event::Character('W'));
    CHECK(f.model->options().n == 2048);
    CHECK(f.model->spectrogram()->options.n == 2048);
    CHECK(line_with(f.h.frame(), "Δf = fs/N").find("21,5") != std::string::npos);
    for (int i = 0; i < 10; ++i) f.h.send(ftxui::Event::Character('W'));
    CHECK(f.model->options().n == 256);
    const auto b = f.screen->area("preset_wide");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min, b.y_min);
    CHECK(f.model->options().n == 512);
}

TEST_CASE("h switches peaks and harmonics, m draws the folding line", "[tui][spectrum]") {
    SpectrumFixture f;
    f.model->set_selection(2.10, 4.85, f.jobs);
    CHECK(f.screen->harmonics_shown());
    CHECK(f.h.frame().find("Гармоники f₀ = 86,1 Гц") != std::string::npos);
    f.h.send(ftxui::Event::Character('h'));
    CHECK_FALSE(f.screen->harmonics_shown());
    CHECK(f.h.frame().find("Пики выделения") != std::string::npos);
    f.h.send(ftxui::Event::Character('m'));
    CHECK(f.screen->fold() == 2);
    CHECK(f.h.frame().find("↓2:") != std::string::npos);
    f.h.send(ftxui::Event::Character('m'));
    f.h.send(ftxui::Event::Character('m'));
    CHECK(f.screen->fold() == 0);
    f.h.send(ftxui::Event::Character('['));
    CHECK(f.screen->floor_db() == -100);
    f.h.send(ftxui::Event::Character('l'));
    CHECK_FALSE(f.screen->log_axis());
}

TEST_CASE("F1 on the spectrum opens the stft card", "[tui][spectrum]") {
    SpectrumFixture f;
    const auto link = f.screen->theory_link();
    REQUIRE(link);
    CHECK(link->kind == TheoryLink::Kind::card);
    CHECK(link->id == "stft");
}

TEST_CASE("the spectrum fits a small terminal", "[tui][spectrum]") {
    SpectrumFixture f{80, 24};
    const auto b = f.screen->area("spectrogram");
    REQUIRE(b.x_max > b.x_min);
    CHECK(b.x_max < 80);
    const auto [x, y] = f.point(0.5, 0.5);
    f.h.click(x, y);
    CHECK(f.screen->cursor());
}

TEST_CASE("a very low sample rate falls back to the linear axis", "[tui][spectrum][review]") {
    signals::core::AudioBuffer low;
    low.sample_rate = 50;
    low.channels.emplace_back(5000);
    for (std::size_t k = 0; k < 5000; ++k) low.channels[0][k] = 0.5 * std::sin(2 * std::numbers::pi * 5 * static_cast<double>(k) / 50);
    const auto path = signals::test::temp_path("spectrum-50hz.wav");
    REQUIRE(signals::io::write_wav(path, low, {}));
    SpectrumFixture f;
    f.signal->open(path, f.jobs);
    auto o = f.model->options();
    o.n = 256;
    f.model->set_options(o, f.jobs);
    f.h.frame();
    REQUIRE(f.model->spectrogram());
    const auto [x, y] = f.point(0.5, 0.0);
    f.h.click(x, y);
    REQUIRE(f.screen->cursor());
    CHECK(f.screen->cursor()->second <= 25.0);
    CHECK(f.screen->cursor()->second > 15.0);
}

TEST_CASE("Esc after a selection restores the whole file", "[tui][spectrum][review]") {
    SpectrumFixture f;
    const auto [x0, y0] = f.point(0.3, 0.5);
    const auto [x1, y1] = f.point(0.5, 0.5);
    f.h.drag(x0, y0, x1, y1, true);
    REQUIRE(f.model->selection());
    CHECK(f.model->selection()->t0 > 0);
    f.h.send(ftxui::Event::Escape);
    REQUIRE(f.model->selection());
    CHECK(f.model->selection()->t0 == 0);
}
