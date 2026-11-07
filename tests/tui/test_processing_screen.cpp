#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>

#include <ftxui/component/event.hpp>

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
