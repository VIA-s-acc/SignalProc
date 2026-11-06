#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <regex>
#include <string>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/harness.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

TEST_CASE("every tab renders at both sizes without nan", "[tui][shell][app]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        JobRunner jobs{[] {}, 0ms, true};
        signals::test::ManualSink* raw = nullptr;
        auto player = signals::test::manual_player(raw);
        auto ui = signals::test::make_ui(w, h);
        ui->jobs = &jobs;
        ui->player = &player;
        auto models = make_app_models();
        models.signal->open(signals::test::sample("test.wav"), jobs);
        Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), w, h};
        const std::pair<char, int> tabs[]{{'1', 1}, {'2', 1}, {'3', 1}, {'4', 2}, {'5', 1}, {'6', 1}, {'7', 1}, {'8', 1}, {'9', 3}};
        for (const auto& [key, subtabs] : tabs) {
            shell.send(ftxui::Event::Character(key));
            for (int s = 0; s < subtabs; ++s) {
                const auto frame = shell.frame();
                INFO("size " << w << "x" << h << " tab " << key << " sub " << s);
                CHECK(frame.find("nan") == std::string::npos);
                CHECK(frame.find("inf") == std::string::npos);
                CHECK_FALSE(std::regex_search(frame, std::regex{R"(\{[^#])"}));
                shell.send(ftxui::Event::Character(']'));
            }
        }
        CHECK(active_tab(shell.root) == 8);
    }
}
