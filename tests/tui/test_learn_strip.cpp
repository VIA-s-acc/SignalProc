#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "learn/progress.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/harness.hpp"
#include "tui/screens/learn_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

struct App {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui;
    AppModels models = make_app_models();
    std::vector<std::shared_ptr<TabScreen>> screens;
    Harness shell;

    App(int w, int h) : ui{signals::test::make_ui(w, h)} {
        ui->jobs = &jobs;
        ui->player = &player;
        models.signal->open(signals::test::sample("test.wav"), jobs);
        screens = make_app_screens(*ui, models);
        shell = Harness{make_shell(*ui, screens, [] {}), w, h};
    }
};

}  // namespace

TEST_CASE("the task strip appears only with an active task", "[tui][learn_strip]") {
    App app{150, 44};
    CHECK(app.shell.frame().find("Собери банк Хаара") == std::string::npos);
    app.models.learn->select("4.3");
    const auto frame = app.shell.frame();
    CHECK(frame.find("4.3 Собери банк Хаара") != std::string::npos);
    CHECK(frame.find("F2") != std::string::npos);
    CHECK(frame.find("F3") != std::string::npos);
}

TEST_CASE("F2 checks the active task from any tab", "[tui][learn_strip]") {
    App app{150, 44};
    app.models.learn->select("4.3");
    app.models.learn->start(*app.ui);
    app.shell.send(ftxui::Event::Character('2'));
    app.shell.send(ftxui::Event::F2);
    const auto v = app.models.learn->view();
    REQUIRE(v.last.has_value());
    CHECK_FALSE(v.last->passed);
    CHECK(app.shell.frame().find("4.3") != std::string::npos);
    CHECK(active_tab(app.shell.root) == 1);
}

TEST_CASE("F2 on an answer task opens the answer field", "[tui][learn_strip]") {
    App app{150, 44};
    app.models.learn->select("1.1");
    app.shell.send(ftxui::Event::F2);
    CHECK(active_tab(app.shell.root) == 8);
    CHECK(app.screens[8]->active_child()->capturing_text());
}

TEST_CASE("F3 opens the next hint and shows it", "[tui][learn_strip]") {
    App app{150, 44};
    app.models.learn->select("4.3");
    app.shell.send(ftxui::Event::F3);
    CHECK(app.models.learn->view().progress.tasks.at("4.3").hints == 1);
    CHECK(app.shell.frame().find("Две ветви") != std::string::npos);
}

TEST_CASE("clicking the strip opens the task", "[tui][learn_strip]") {
    App app{150, 44};
    app.models.learn->select("4.4");
    const auto frame = app.shell.frame();
    const auto at = signals::test::locate(frame, "4.4 Задержка");
    REQUIRE(at.has_value());
    app.shell.click(at->first, at->second);
    CHECK(active_tab(app.shell.root) == 8);
    const auto* learn = dynamic_cast<LearnScreen*>(app.screens[8]->active_child().get());
    REQUIRE(learn != nullptr);
    CHECK(learn->cursor() == "4.4");
}

TEST_CASE("task strip snapshots", "[tui][learn_strip][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        App app{w, h};
        app.models.learn->select("4.3");
        app.shell.send(ftxui::Event::Character('3'));
        signals::test::check_snapshot("strip_filters_" + std::to_string(w) + "x" + std::to_string(h), app.shell.frame());
    }
}
