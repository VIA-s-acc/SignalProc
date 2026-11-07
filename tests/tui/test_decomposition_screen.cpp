#include <catch2/catch_approx.hpp>
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
#include "tui/widgets/screen_util.hpp"

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

TEST_CASE("v cycles subbands, MRA, tiles and packets", "[tui][decomposition]") {
    DecompositionFixture f;
    CHECK(f.screen->view() == DecompositionScreen::View::subbands);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::mra);
    CHECK(f.h.frame().find("MRA · x = d₁ + … + a₆") != std::string::npos);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::tiles);
    CHECK(f.h.frame().find("Плитки t–f") != std::string::npos);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->view() == DecompositionScreen::View::packets);
    CHECK(f.h.frame().find("Пакеты · клик по узлу") != std::string::npos);
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
    f.click("chip_scalogram");
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

TEST_CASE("z shows the focused decomposition panel on the whole screen", "[tui][decomposition][zoom]") {
    DecompositionFixture f;
    f.h.send(ftxui::Event::Tab);
    CHECK(f.h.send(ftxui::Event::Character('z')));
    auto frame = f.h.frame();
    CHECK(frame.find("Энергия по полосам") != std::string::npos);
    CHECK(frame.find("Прослушивание") == std::string::npos);
    CHECK(f.h.send(ftxui::Event::Escape));
    CHECK(f.h.frame().find("Прослушивание") != std::string::npos);
}

TEST_CASE("decomposition keys move the cursor, zoom time, change levels and edges", "[tui][decomposition]") {
    DecompositionFixture f;
    const auto [b0, e0] = f.screen->time_view();
    f.h.send(ftxui::Event::ArrowRight);
    REQUIRE(f.screen->cursor());
    const double c = *f.screen->cursor();
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(*f.screen->cursor() > c);
    CHECK(f.h.send(ftxui::Event::Character('+')));
    const auto [b1, e1] = f.screen->time_view();
    CHECK(e1 - b1 < e0 - b0);
    f.h.send(ftxui::Event::Character('0'));
    const auto [b2, e2] = f.screen->time_view();
    CHECK(e2 - b2 == Catch::Approx(e0 - b0));
    const auto levels = f.model->options().levels;
    CHECK(f.h.send(ftxui::Event::Character('j')));
    CHECK(f.model->options().levels == levels + 1);
    f.h.send(ftxui::Event::Character('J'));
    CHECK(f.model->options().levels == levels);
    const auto mode = f.model->options().mode;
    CHECK(f.h.send(ftxui::Event::Character('e')));
    CHECK(f.model->options().mode != mode);
}

TEST_CASE("the band list follows the selected band when it does not fit", "[tui][decomposition]") {
    DecompositionFixture f{true, 80, 24};
    for (int i = 0; i < 6; ++i) f.h.send(ftxui::Event::Character('j'));
    REQUIRE(f.model->options().levels >= 12);
    auto frame = f.h.frame();
    CHECK(frame.find("ещё") != std::string::npos);
    for (int i = 0; i < 30; ++i) f.h.send(ftxui::Event::ArrowDown);
    frame = f.h.frame();
    const auto last = band_name(0, f.model->options().levels, GlyphMode::unicode);
    CHECK(frame.find(last) != std::string::npos);
}

namespace {

void deepen(DecompositionFixture& f, std::size_t levels) {
    while (f.model->options().levels < levels) {
        const auto before = f.model->options().levels;
        f.h.send(ftxui::Event::Character('j'));
        if (f.model->options().levels == before) break;
    }
    f.h.frame();
}

}  // namespace

TEST_CASE("the wheel over the band labels scrolls the list, over the plot it zooms", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    auto frame = f.h.frame();
    REQUIRE(frame.find("☑ d₁ ") != std::string::npos);
    const auto labels = f.screen->area("band_labels");
    REQUIRE(labels.x_max >= labels.x_min);
    const auto view = f.screen->time_view();
    f.h.wheel(labels.x_min + 2, labels.y_min + 3, false);
    f.h.wheel(labels.x_min + 2, labels.y_min + 3, false);
    frame = f.h.frame();
    CHECK(frame.find("☑ d₁ ") == std::string::npos);
    CHECK(frame.find("▲ ещё 2") != std::string::npos);
    CHECK(f.screen->time_view() == view);
    f.h.wheel(labels.x_min + 2, labels.y_min + 3, true);
    f.h.wheel(labels.x_min + 2, labels.y_min + 3, true);
    CHECK(f.h.frame().find("☑ d₁ ") != std::string::npos);
    const auto plot = f.screen->area("view_plot");
    f.h.wheel(plot.x_min + 10, plot.y_min + 1, true);
    CHECK(f.screen->time_view() != view);
}

TEST_CASE("the wheel scrolls the energy list without moving the band list", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    auto frame = f.h.frame();
    REQUIRE(line_with(frame, "d₁  11").find("%") != std::string::npos);
    const auto energy = f.screen->area("energy_panel");
    REQUIRE(energy.x_max >= energy.x_min);
    for (int i = 0; i < 3; ++i) f.h.wheel(energy.x_min + 4, energy.y_min + 2, false);
    frame = f.h.frame();
    CHECK(line_with(frame, "d₁  11").empty());
    CHECK(line_with(frame, "d₄  1,38к").find("%") != std::string::npos);
    CHECK(frame.find("☑ d₁ ") != std::string::npos);
}

TEST_CASE("arrows bring a scrolled-away band back into view", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    const auto labels = f.screen->area("band_labels");
    for (int i = 0; i < 5; ++i) f.h.wheel(labels.x_min + 2, labels.y_min + 3, false);
    REQUIRE(f.h.frame().find("☑ d₁ ") == std::string::npos);
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowUp);
    CHECK(f.h.frame().find("☑ d₁ ") != std::string::npos);
}

TEST_CASE("the cursor panel scrolls when the band values do not fit", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    f.h.send(ftxui::Event::Character('v'));
    for (int i = 0; i < 200 && f.model->recon() && f.model->recon()->components.empty(); ++i) {
        std::this_thread::sleep_for(5ms);
        f.h.frame();
    }
    f.h.send(ftxui::Event::ArrowRight);
    auto frame = f.h.frame();
    REQUIRE(frame.find("x  ") != std::string::npos);
    CHECK(frame.find("▼ ещё") != std::string::npos);
    const auto cursor = f.screen->area("cursor_panel");
    REQUIRE(cursor.x_max >= cursor.x_min);
    for (int i = 0; i < 6; ++i) f.h.wheel(cursor.x_min + 4, cursor.y_min + 2, false);
    frame = f.h.frame();
    CHECK(frame.find("a₁₇") != std::string::npos);
}

TEST_CASE("a narrow energy panel keeps the percent column whole", "[tui][decomposition]") {
    DecompositionFixture f{true, 120, 40};
    deepen(f, 17);
    const auto frame = f.h.frame();
    const auto row = line_with(frame.substr(frame.find("Энергия")), "d₁");
    CHECK(row.find("0,79 %") != std::string::npos);
}

TEST_CASE("the summary names disabled bands as compact ranges", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    for (int i = 0; i < 3; ++i) {
        f.h.send(ftxui::Event::Return);
        f.h.send(ftxui::Event::ArrowDown);
    }
    for (int i = 0; i < 200 && f.model->recon() && f.model->recon()->enabled.size() > 0 && f.model->recon()->enabled[2]; ++i) {
        std::this_thread::sleep_for(5ms);
        f.h.frame();
    }
    const auto line = line_with(f.h.frame(), "в синтезе");
    CHECK(line.find("d₄–a₁₇") != std::string::npos);
}

TEST_CASE("the lambda list stays inside the denoise panel", "[tui][decomposition]") {
    DecompositionFixture f;
    deepen(f, 17);
    f.h.send(ftxui::Event::Tab);
    f.h.send(ftxui::Event::Tab);
    f.h.send(ftxui::Event::Tab);
    f.h.send(ftxui::Event::Return);
    for (int i = 0; i < 200 && f.model->recon() && !f.model->recon()->processing.rule; ++i) {
        std::this_thread::sleep_for(5ms);
        f.h.frame();
    }
    const auto frame = f.h.frame();
    const auto line = line_with(frame, "λ:");
    REQUIRE_FALSE(line.empty());
    const auto tail = line.substr(line.find("λ:"));
    const auto inside = tail.substr(0, tail.find("│"));
    CHECK((inside.back() == ' ' || inside.ends_with("…")));
}

TEST_CASE("t switches DWT, SWT and MODWT with undecimated bands and exact reconstruction", "[tui][decomposition][swt]") {
    DecompositionFixture f;
    CHECK(f.model->options().transform == Transform::dwt);
    CHECK(f.h.send(ftxui::Event::Character('t')));
    CHECK(f.model->options().transform == Transform::swt);
    f.h.frame();
    auto d = f.model->data();
    REQUIRE(d);
    CHECK(d->options.transform == Transform::swt);
    for (const auto& b : d->bands) CHECK(b.length >= d->x.size());
    CHECK(d->pr_error < 1e-9);
    f.h.send(ftxui::Event::Character('t'));
    f.h.frame();
    d = f.model->data();
    REQUIRE(d);
    CHECK(d->options.transform == Transform::modwt);
    CHECK(d->pr_error < 1e-9);
    double total = 0;
    for (const auto& b : d->bands) total += b.energy_pct;
    CHECK(std::abs(total - 100) < 1e-6);
    f.click("chip_dwt");
    CHECK(f.model->options().transform == Transform::dwt);
}

TEST_CASE("the denoise panel works on SWT bands", "[tui][decomposition][swt]") {
    DecompositionFixture f;
    f.h.send(ftxui::Event::Character('t'));
    f.h.frame();
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::Tab);
    f.h.send(ftxui::Event::Return);
    f.h.frame();
    const auto r = f.model->recon();
    REQUIRE(r);
    CHECK(r->processing.rule);
    CHECK(r->energy_kept_pct < 100);
}

TEST_CASE("the packets chip shows the packet table with the best basis", "[tui][decomposition][packets]") {
    DecompositionFixture f;
    f.click("chip_packets");
    CHECK(f.screen->view() == DecompositionScreen::View::packets);
    f.h.frame();
    const auto p = f.model->packets();
    REQUIRE(p);
    const auto J = f.model->options().levels;
    CHECK(p->table.size() == J + 1);
    for (std::size_t j = 0; j <= J; ++j) CHECK(p->table[j].size() == (std::size_t{1} << j));
    CHECK(f.model->basis() == p->best);
    CHECK(signals::core::admissible(f.model->basis(), J));
    CHECK(p->best_cost <= p->dwt_cost + 1e-12);
    CHECK(p->best_cost <= p->root_cost + 1e-12);
    CHECK(f.model->basis_pr_error() < 1e-9);
    const auto frame = f.h.frame();
    CHECK(frame.find("L0") != std::string::npos);
    CHECK(frame.find("L" + std::to_string(J)) != std::string::npos);
}

TEST_CASE("make_leaf keeps the basis admissible and B, W restore presets", "[tui][decomposition][packets]") {
    DecompositionFixture f;
    f.click("chip_packets");
    f.h.frame();
    REQUIRE(f.model->packets());
    const auto J = f.model->options().levels;
    f.model->make_leaf({1, 0});
    auto b = f.model->basis();
    CHECK(signals::core::admissible(b, J));
    CHECK(std::ranges::find(b, signals::core::PacketNode{1, 0}) != b.end());
    CHECK(f.model->basis_pr_error() < 1e-9);
    const signals::core::PacketNode deep{J, (std::size_t{1} << J) - 1};
    f.model->make_leaf(deep);
    b = f.model->basis();
    CHECK(signals::core::admissible(b, J));
    CHECK(std::ranges::find(b, deep) != b.end());
    CHECK(std::ranges::find(b, signals::core::PacketNode{1, 0}) != b.end());
    f.h.send(ftxui::Event::Character('W'));
    CHECK(f.model->basis() == signals::core::dwt_basis(J));
    CHECK(f.model->basis_pr_error() < 1e-9);
    f.h.send(ftxui::Event::Character('B'));
    CHECK(f.model->basis() == f.model->packets()->best);
    CHECK_FALSE(f.model->set_basis({{1, 0}}));
}

TEST_CASE("arrows and Enter pick a packet node on the keyboard", "[tui][decomposition][packets]") {
    DecompositionFixture f;
    f.click("chip_packets");
    f.h.frame();
    REQUIRE(f.model->packets());
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowRight);
    f.h.send(ftxui::Event::Return);
    const auto b = f.model->basis();
    CHECK(std::ranges::find(b, signals::core::PacketNode{1, signals::core::natural_index_at(1, 1)}) != b.end());
}

TEST_CASE("a click on a narrow packet table picks the node under the mouse", "[tui][decomposition][packets]") {
    DecompositionFixture f{true, 80, 24};
    auto o = f.model->options();
    o.levels = 12;
    f.model->set_options(o, f.jobs);
    f.h.frame();
    f.screen->set_view(DecompositionScreen::View::packets);
    const auto frame = f.h.frame();
    REQUIRE(f.model->packets());
    CHECK(frame.find("L12") != std::string::npos);
    const auto table = f.screen->area("packet_table");
    REQUIRE(table.x_max > table.x_min);
    CHECK(table.x_max < 80);
    const int width = table.x_max - table.x_min + 1;
    const auto row = f.screen->packet_row(12);
    REQUIRE(row);
    f.h.click(table.x_min + 2, *row);
    const auto k = (2 * (std::size_t{1} << 12) + (std::size_t{1} << 11)) / static_cast<std::size_t>(width);
    const auto b = f.model->basis();
    CHECK(std::ranges::find(b, signals::core::PacketNode{12, signals::core::natural_index_at(12, k)}) != b.end());
}

TEST_CASE("a new wavelet recomputes packets and hides the stale table", "[tui][decomposition][packets]") {
    DecompositionFixture f;
    f.click("chip_packets");
    f.h.frame();
    REQUIRE(f.model->packets());
    auto o = f.model->options();
    o.wavelet = "db4";
    f.model->set_options(o, f.jobs);
    CHECK_FALSE(f.model->packets());
    f.h.frame();
    const auto p = f.model->packets();
    REQUIRE(p);
    CHECK(p->options.wavelet == "db4");
}

