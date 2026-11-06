#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <regex>
#include <string>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/harness.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/text_util.hpp"

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
                CHECK_FALSE(std::regex_search(frame, std::regex{"M[0-4][ab]?\\."}));
                shell.send(ftxui::Event::Character(']'));
            }
        }
        CHECK(active_tab(shell.root) == 8);
    }
}

TEST_CASE("tab and shift+tab switch the maximized panel on every screen", "[tui][shell][app]") {
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
        for (char key : {'1', '3', '4', '5', '6', '9'}) {
            INFO("size " << w << "x" << h << " tab " << key);
            shell.send(ftxui::Event::Character(key));
            shell.frame();
            shell.send(ftxui::Event::Character('z'));
            const auto first = shell.frame();
            shell.send(ftxui::Event::Tab);
            const auto second = shell.frame();
            CHECK(second != first);
            shell.send(ftxui::Event::TabReverse);
            CHECK(shell.frame() == first);
            shell.send(ftxui::Event::Escape);
            shell.frame();
        }
    }
}

TEST_CASE("the hint row shows tab and zoom keys and no mouse hints at small sizes", "[tui][shell][app]") {
    for (auto [w, h] : {std::pair{80, 24}, std::pair{117, 40}}) {
        JobRunner jobs{[] {}, 0ms, true};
        signals::test::ManualSink* raw = nullptr;
        auto player = signals::test::manual_player(raw);
        auto ui = signals::test::make_ui(w, h);
        ui->jobs = &jobs;
        ui->player = &player;
        auto models = make_app_models();
        models.signal->open(signals::test::sample("test.wav"), jobs);
        Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), w, h};
        for (char key : {'1', '3', '4', '5', '6', '9'}) {
            INFO("size " << w << "x" << h << " tab " << key);
            shell.send(ftxui::Event::Character(key));
            const auto frame = shell.frame();
            std::vector<std::string> lines;
            for (std::size_t at = 0, next; at < frame.size(); at = next + 1) {
                next = frame.find('\n', at);
                if (next == std::string::npos) next = frame.size();
                lines.push_back(frame.substr(at, next - at));
            }
            REQUIRE(lines.size() >= 2);
            const auto& hints = lines[lines.size() - 2];
            CHECK(hints.find(" ? ") != std::string::npos);
            CHECK(hints.find("колесо") == std::string::npos);
            CHECK(hints.find("клик") == std::string::npos);
            if (key == '9') CHECK(hints.find(" [ ] ") != std::string::npos);
            else CHECK(hints.find(" Tab ") != std::string::npos);
        }
    }
}

TEST_CASE("the tab bar context is clipped instead of running into the tabs", "[tui][shell][app]") {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    auto ui = signals::test::make_ui(80, 24);
    ui->jobs = &jobs;
    ui->player = &player;
    auto models = make_app_models();
    Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), 80, 24};
    for (char key : {'4', '9'}) {
        shell.send(ftxui::Event::Character(key));
        const auto frame = shell.frame();
        const auto first = frame.substr(0, frame.find('\n'));
        INFO(first);
        CHECK(static_cast<int>(signals::tui::split_codepoints(first).size()) <= 80);
        CHECK(first.find(key == '9' ? "Теория " : "Банк ") != std::string::npos);
        CHECK(first.find("[") != std::string::npos);
    }
}

TEST_CASE("m opens the waveform menu at cursor a without the mouse", "[tui][shell][app][menu]") {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->player = &player;
    auto models = make_app_models();
    models.signal->open(signals::test::sample("test.wav"), jobs);
    Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), 150, 44};
    shell.frame();
    shell.send(ftxui::Event::ArrowRight);
    shell.frame();
    CHECK(shell.send(ftxui::Event::Character('m')));
    CHECK(shell.frame().find(signals::tr("tui.menu.whole_signal")) != std::string::npos);
}
