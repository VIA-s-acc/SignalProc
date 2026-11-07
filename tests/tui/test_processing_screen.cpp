#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>

#include "core/design/families.hpp"
#include "audio/manual_sink.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/processing_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

struct ProcessingFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<ProcessingModel> model;
    std::shared_ptr<ProcessingScreen> screen;
    Harness h;

    explicit ProcessingFixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)},
          model{(ui->jobs = &jobs, signal->open(signals::test::sample("test.wav"), jobs), std::make_shared<ProcessingModel>(signal))},
          screen{std::make_shared<ProcessingScreen>(*ui, model)},
          h{screen, w, hgt - 3} {
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

TEST_CASE("processing snapshots", "[tui][processing][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        ProcessingFixture f{w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        shell.frame();
        signals::test::check_snapshot("processing_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("m cycles the four rules and the summary follows", "[tui][processing]") {
    ProcessingFixture f;
    REQUIRE(f.model->result());
    CHECK(f.model->options().rule == signals::core::ShrinkRule::bayes);
    for (const auto rule : {signals::core::ShrinkRule::visu, signals::core::ShrinkRule::sure, signals::core::ShrinkRule::minimax, signals::core::ShrinkRule::bayes}) {
        f.h.send(ftxui::Event::Character('m'));
        CHECK(f.model->options().rule == rule);
        const auto r = f.model->result();
        REQUIRE(r);
        CHECK(r->options.rule == rule);
        CHECK(f.h.frame().find(std::string{signals::core::shrink_rule_name(rule)} + " · 1") != std::string::npos);
    }
    f.h.send(ftxui::Event::Character('u'));
    CHECK(f.model->options().rule == signals::core::ShrinkRule::minimax);
}

TEST_CASE("brackets step the lambda scale and s toggles hard", "[tui][processing]") {
    ProcessingFixture f;
    f.h.send(ftxui::Event::Character(']'));
    CHECK(f.model->options().scale == 1.5);
    f.h.send(ftxui::Event::Character('['));
    f.h.send(ftxui::Event::Character('['));
    CHECK(f.model->options().scale == 0.75);
    f.click("scale_2,00");
    CHECK(f.model->options().scale == 2.0);
    f.h.send(ftxui::Event::Character('s'));
    CHECK(f.model->options().threshold == signals::core::ThresholdMode::hard);
}

TEST_CASE("e cycles band presets and aliasing appears", "[tui][processing]") {
    ProcessingFixture f;
    const auto flat = f.model->result();
    REQUIRE(flat);
    double flat_alias = -1e9;
    for (const double a : flat->transfer.a_db) flat_alias = std::max(flat_alias, a);
    f.h.send(ftxui::Event::Character('e'));
    CHECK(f.screen->preset() == 1);
    const auto bass = f.model->result();
    REQUIRE(bass);
    double bass_alias = -1e9;
    for (const double a : bass->transfer.a_db) bass_alias = std::max(bass_alias, a);
    CHECK(bass_alias > flat_alias + 20);
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::Character('e'));
    CHECK(f.screen->preset() == 0);
    CHECK(f.model->options().gains.empty());
}

TEST_CASE("a, b and x switch the signal view", "[tui][processing]") {
    ProcessingFixture f;
    f.h.send(ftxui::Event::Character('b'));
    CHECK(f.screen->view() == ProcessingScreen::View::b);
    CHECK(f.h.frame().find("Сигнал · B результат") != std::string::npos);
    f.h.send(ftxui::Event::Character('x'));
    CHECK(f.screen->view() == ProcessingScreen::View::x);
    CHECK(f.screen->theory_link()->id == "threshold");
}

TEST_CASE("a, b and x take over only their own clip", "[tui][processing][review]") {
    ProcessingFixture f;
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    f.ui->player = &player;
    std::vector<float> other(441000, 0.1F);
    signals::audio::Clip clip;
    clip.channels.emplace_back(other);
    clip.rate = 44100;
    clip.tag = "signal";
    REQUIRE(player.play(clip, 400000));
    f.h.send(ftxui::Event::Character('a'));
    CHECK(player.tag().starts_with("processing"));
    CHECK(player.state() == signals::audio::PlayState::playing);
    f.h.send(ftxui::Event::Character('b'));
    CHECK(player.tag() == "processing.b");
    f.ui->player = nullptr;
}

TEST_CASE("j and J change the levels and the decomposition link passes its wavelet", "[tui][processing][review]") {
    ProcessingFixture f;
    f.h.send(ftxui::Event::Character('J'));
    CHECK(f.model->options().levels == 4);
    f.h.send(ftxui::Event::Character('j'));
    CHECK(f.model->options().levels == 5);
    NavArgs args;
    args.wavelet = "db4";
    args.n = 3;
    f.screen->open_with(args);
    CHECK(f.model->options().wavelet == "db4");
    CHECK(f.model->options().levels == 3);
    REQUIRE(f.model->result());
    CHECK(f.model->result()->options.wavelet == "db4");
}

TEST_CASE("the signal selection becomes the fragment", "[tui][processing][review]") {
    ProcessingFixture f;
    f.signal->select(1.0, 1.5);
    f.h.frame();
    const auto r = f.model->result();
    REQUIRE(r);
    CHECK(r->start == 44100);
    CHECK(r->clean.size() == 22050);
    CHECK(f.screen->context().find("1,00–1,50 с") != std::string::npos);
}

namespace {

ftxui::Event left(ftxui::Mouse::Motion motion, int x, int y) {
    ftxui::Mouse m;
    m.button = ftxui::Mouse::Left;
    m.motion = motion;
    m.x = x;
    m.y = y;
    return ftxui::Event::Mouse("", m);
}

}  // namespace

TEST_CASE("m7c: one u undoes a whole λ drag, and dragging back to the press row restores λ", "[tui][processing]") {
    ProcessingFixture f;
    const auto b = f.screen->area("coeffs_panel");
    REQUIRE(b.x_max > b.x_min);
    const int x = (b.x_min + b.x_max) / 2, y = (b.y_min + b.y_max) / 2;
    REQUIRE(f.model->options().scale == 1.0);
    f.h.drag(x, y, x, y - 4);
    CHECK(f.model->options().scale > 1.5);
    f.h.send(ftxui::Event::Character('u'));
    CHECK(f.model->options().scale == 1.0);
    f.h.send(left(ftxui::Mouse::Pressed, x, y));
    f.h.send(left(ftxui::Mouse::Moved, x, y - 2));
    f.h.send(left(ftxui::Mouse::Moved, x, y));
    f.h.send(left(ftxui::Mouse::Released, x, y));
    CHECK(f.model->options().scale == 1.0);
}

TEST_CASE("m7c: u clears the preset highlight it no longer matches", "[tui][processing]") {
    ProcessingFixture f;
    f.h.send(ftxui::Event::Character('e'));
    REQUIRE_FALSE(f.model->options().gains.empty());
    CHECK(f.screen->active_preset() == 1);
    f.h.send(ftxui::Event::Character('u'));
    REQUIRE(f.model->options().gains.empty());
    CHECK(f.screen->active_preset() == 0);
}

TEST_CASE("m7c: the second toolbar row at 80×24 ends with an ellipsis instead of a cut word", "[tui][processing]") {
    ProcessingFixture f{80, 24};
    const auto frame = f.h.frame();
    const auto row = frame.substr(frame.find("λ ×"), frame.find('\n', frame.find("λ ×")) - frame.find("λ ×"));
    INFO(row);
    CHECK(row.find("…") != std::string::npos);
}

TEST_CASE("m7d: a λ drag that starts at the limit still undoes in one step", "[tui][processing]") {
    ProcessingFixture f;
    auto o = f.model->options();
    o.scale = 10;
    f.model->set_options(o, f.jobs);
    o.rule = signals::core::ShrinkRule::visu;
    f.model->set_options(o, f.jobs);
    const auto b = f.screen->area("coeffs_panel");
    const int x = (b.x_min + b.x_max) / 2, y = (b.y_min + b.y_max) / 2;
    f.h.send(left(ftxui::Mouse::Pressed, x, y));
    f.h.send(left(ftxui::Mouse::Moved, x, y - 1));
    f.h.send(left(ftxui::Mouse::Moved, x, y + 2));
    f.h.send(left(ftxui::Mouse::Released, x, y + 2));
    REQUIRE(f.model->options().scale < 10);
    f.h.send(ftxui::Event::Character('u'));
    CHECK(f.model->options().scale == 10);
    CHECK(f.model->options().rule == signals::core::ShrinkRule::visu);
}

TEST_CASE("the listening panel says what A, B and X play", "[tui][processing]") {
    ProcessingFixture f;
    const auto frame = f.h.frame();
    CHECK(frame.find("звучат 4,00 с") != std::string::npos);
}

TEST_CASE("every processing plot zooms with the wheel and resets with 0", "[tui][processing][panels]") {
    ProcessingFixture f;
    f.h.frame();
    for (const char* id : {"signal", "curve", "eq", "coeffs"}) {
        INFO(id);
        const auto b = f.screen->area(std::string{id} + "_plot");
        REQUIRE(b.x_max > b.x_min);
        const auto full = f.screen->plot_view(id);
        REQUIRE(full);
        const int x = b.x_min + (b.x_max - b.x_min) / 3, y = (b.y_min + b.y_max) / 2;
        f.h.wheel(x, y, true);
        f.h.wheel(x, y, true);
        f.h.frame();
        const auto zoomed = f.screen->plot_view(id);
        REQUIRE(zoomed);
        const bool log = full->scale == signals::tui::AxisScale::log10;
        const double span = log ? std::log(zoomed->hi / zoomed->lo) : zoomed->hi - zoomed->lo;
        const double whole = log ? std::log(full->hi / full->lo) : full->hi - full->lo;
        CHECK(span < 0.7 * whole);
        CHECK_FALSE(f.screen->context_menu(x, y).empty());
        const double scale = f.model->options().scale;
        f.h.click(x, y);
        f.h.send(ftxui::Event::Character('0'));
        f.h.frame();
        const auto reset = f.screen->plot_view(id);
        REQUIRE(reset);
        CHECK(reset->lo == full->lo);
        CHECK(reset->hi == full->hi);
        CHECK(f.model->options().scale == scale);
    }
}

TEST_CASE("every coefficient band stays reachable: the list scrolls with markers and the wheel", "[tui][processing][scroll]") {
    ProcessingFixture f{120, 40};
    for (int k = 0; k < 6; ++k) f.h.send(ftxui::Event::Character('j'));
    REQUIRE(f.model->options().levels == 10);
    auto frame = f.h.frame();
    CHECK(frame.find("▼ ещё") != std::string::npos);
    const auto panel = f.screen->area("coeffs_panel");
    REQUIRE(panel.x_max > panel.x_min);
    for (int k = 0; k < 10; ++k) f.h.wheel(panel.x_min + 3, (panel.y_min + panel.y_max) / 2, false);
    frame = f.h.frame();
    CHECK(frame.find("▲ ещё") != std::string::npos);
    CHECK(frame.find("d₁ ") != std::string::npos);
}

TEST_CASE("the processing wavelet is picked from a list that includes the user banks", "[tui][processing][user_banks]") {
    const auto db2 = signals::core::wavelet("db2");
    REQUIRE(db2);
    signals::core::set_user_wavelets([db2](std::string_view name) -> std::optional<signals::core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db2;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    ProcessingFixture f;
    std::string filter;
    f.ui->open_palette = [&](std::string text) { filter = std::move(text); };
    f.h.send(ftxui::Event::Character('w'));
    CHECK(filter == signals::tr("tui.processing.cmd.wavelet"));
    const auto commands = f.screen->commands();
    const auto mine = std::find_if(commands.begin(), commands.end(), [](const Command& c) { return c.id == "processing.wavelet.my_bank"; });
    REQUIRE(mine != commands.end());
    CHECK(std::any_of(commands.begin(), commands.end(), [](const Command& c) { return c.id == "processing.wavelet.sym8"; }));
    mine->run();
    CHECK(f.model->options().wavelet == "my_bank");
    REQUIRE(f.model->result());
    CHECK(f.model->result()->options.wavelet == "my_bank");
    CHECK(f.h.frame().find("my_bank ▾") != std::string::npos);
    signals::core::set_user_wavelets({}, {});
}

TEST_CASE("the coefficient bands scroll from the keyboard when the panel has focus", "[tui][processing][scroll]") {
    ProcessingFixture f{120, 40};
    for (int k = 0; k < 6; ++k) f.h.send(ftxui::Event::Character('j'));
    f.h.frame();
    const auto panel = f.screen->area("coeffs_panel");
    f.h.click(panel.x_min + 3, panel.y_min + 1);
    f.h.send(ftxui::Event::PageDown);
    CHECK(f.h.frame().find("▲ ещё") != std::string::npos);
    f.h.send(ftxui::Event::Home);
    CHECK(f.h.frame().find("▲ ещё") == std::string::npos);
}
