#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>

#include "audio/manual_sink.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/harness.hpp"
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

int requested_rows(TabScreen& screen) {
    auto element = screen.Render();
    element->ComputeRequirement();
    return element->requirement().min_y;
}

}  // namespace

TEST_CASE("the task list reaches its last task while the task strip is shown", "[tui][learn_strip]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        App app{w, h};
        app.models.learn->select("0.1");
        app.shell.send(ftxui::Event::Character('9'));
        for (int k = 0; k < 3; ++k) app.shell.send(ftxui::Event::Character(']'));
        for (int k = 0; k < 40; ++k) app.shell.send(ftxui::Event::PageDown);
        const auto& last = app.models.learn->course().chapters.back().tasks.back();
        INFO("size " << w << "x" << h);
        CHECK(app.shell.frame().find("· " + last.id + " ") != std::string::npos);
    }
}

TEST_CASE("no screen asks for more rows than the shell leaves it", "[tui][learn_strip]") {
    const std::tuple<char, int, char> tabs[]{{'1', 1, ']'}, {'2', 2, '}'}, {'3', 1, ']'}, {'4', 2, ']'}, {'5', 1, ']'},
                                             {'6', 1, ']'}, {'7', 1, ']'}, {'8', 1, ']'}, {'9', 4, ']'}};
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}})
        for (const bool strip : {false, true}) {
            App app{w, h};
            if (strip) app.models.learn->select("0.1");
            const int body = h - 3 - (strip ? 1 : 0);
            for (const auto& [key, subtabs, next] : tabs) {
                app.shell.send(ftxui::Event::Character(key));
                for (int s = 0; s < subtabs; ++s) {
                    app.shell.frame();
                    INFO("size " << w << "x" << h << " strip " << strip << " tab " << key << " sub " << s);
                    CHECK(requested_rows(*app.screens[static_cast<std::size_t>(key - '1')]) <= body);
                    app.shell.send(ftxui::Event::Character(next));
                }
            }
        }
}
