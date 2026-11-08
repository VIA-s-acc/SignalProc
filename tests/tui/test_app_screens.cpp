#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <regex>
#include <tuple>
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
        const std::tuple<char, int, char> tabs[]{{'1', 1, ']'}, {'2', 2, '}'}, {'3', 1, ']'}, {'4', 2, ']'}, {'5', 1, ']'},
                                                 {'6', 1, ']'}, {'7', 1, ']'}, {'8', 1, ']'}, {'9', 3, ']'}};
        for (const auto& [key, subtabs, next] : tabs) {
            shell.send(ftxui::Event::Character(key));
            for (int s = 0; s < subtabs; ++s) {
                const auto frame = shell.frame();
                INFO("size " << w << "x" << h << " tab " << key << " sub " << s);
                CHECK(frame.find("nan") == std::string::npos);
                CHECK(frame.find("inf") == std::string::npos);
                CHECK_FALSE(std::regex_search(frame, std::regex{R"(\{[a-z_.]+\})"}));
                CHECK_FALSE(std::regex_search(frame, std::regex{"M[0-4][ab]?\\."}));
                shell.send(ftxui::Event::Character(next));
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

TEST_CASE("every tab renders in English without Russian text", "[tui][shell][app][i18n]") {
    const signals::LanguageScope en{signals::Language::en};
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->player = &player;
    auto models = make_app_models();
    models.signal->open(signals::test::sample("test.wav"), jobs);
    Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), 150, 44};
    const auto cyrillic = [](const std::string& s) {
        for (unsigned char c : s)
            if (c == 0xD0 || c == 0xD1) return true;
        return false;
    };
    const std::pair<char, int> tabs[]{{'1', 1}, {'2', 1}, {'3', 1}, {'4', 2}, {'5', 1}, {'6', 1}, {'7', 1}, {'8', 1}, {'9', 3}};
    for (const auto& [key, subtabs] : tabs) {
        shell.send(ftxui::Event::Character(key));
        for (int s = 0; s < subtabs; ++s) {
            shell.frame();
            const auto frame = shell.frame();
            INFO("tab " << key << " sub " << s << "\n" << frame);
            CHECK_FALSE(cyrillic(frame));
            shell.send(ftxui::Event::Character(']'));
        }
    }
    shell.send(ftxui::Event::Character('?'));
    const auto help = shell.frame();
    INFO(help);
    CHECK_FALSE(cyrillic(help));
}

TEST_CASE("tab 8 is the pipeline editor", "[tui][shell][app]") {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->player = &player;
    auto models = make_app_models();
    Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), 150, 44};
    shell.send(ftxui::Event::Character('8'));
    const auto frame = shell.frame();
    CHECK(frame.find(signals::tr("tui.pipeline.graph")) != std::string::npos);
    CHECK(frame.find(signals::tr("tui.placeholder.pipeline.text")) == std::string::npos);
}


TEST_CASE("every control on every screen explains itself on hover in both languages", "[tui][app][explain]") {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->player = &player;
    auto models = make_app_models();
    models.signal->open(signals::test::sample("test.wav"), jobs);
    auto screens = make_app_screens(*ui, models);
    // Sub-tab switches are jump targets too; their label already names the screen they open.
    std::vector<std::string> allowed;
    for (const char* key : {"tui.sub.spectrogram", "tui.sub.resample", "tui.sub.tempo", "tui.sub.designer", "tui.sub.diagram", "tui.sub.explain", "tui.sub.under_the_hood",
                            "tui.sub.tutorial"})
        allowed.push_back(signals::tr(key));
    const std::pair<std::size_t, std::size_t> tabs[]{{0, 1}, {1, 3}, {2, 1}, {3, 2}, {4, 1}, {5, 1}, {6, 1}, {8, 3}};
    std::size_t checked = 0;
    for (const auto& [tab, subtabs] : tabs) {
        auto& screen = *screens[tab];
        signals::test::Harness h{screens[tab], 150, 41};
        for (std::size_t s = 0; s < subtabs; ++s) {
            screen.select_child(s);
            h.frame();
            h.frame();
            for (const auto& t : screen.jump_targets()) {
                if (t.box.x_max < t.box.x_min || t.box.y_max < t.box.y_min) continue;
                if (std::find(allowed.begin(), allowed.end(), t.label) != allowed.end()) continue;
                const int x = (t.box.x_min + t.box.x_max) / 2, y = (t.box.y_min + t.box.y_max) / 2;
                INFO("tab " << tab << " sub " << s << " target " << t.label << " at " << x << "," << y);
                CHECK(screen.explain_at(x, y).has_value());
                const signals::LanguageScope en{signals::Language::en};
                CHECK(screen.explain_at(x, y).has_value());
                ++checked;
            }
        }
    }
    CHECK(checked > 150);
}
