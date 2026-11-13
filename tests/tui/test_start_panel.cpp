#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/data/session.hpp"
#include "tui/harness.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

struct StartFixture {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui;
    AppModels models = make_app_models();
    fs::path session_file;
    std::optional<Harness> shell;

    StartFixture(bool fresh, int w = 150, int h = 44, GlyphMode glyphs = GlyphMode::unicode, ColorDepth depth = ColorDepth::truecolor)
        : ui{signals::test::make_ui(w, h, glyphs, depth)} {
        const auto dir = signals::test::temp_path("start-panel");
        fs::remove_all(dir);
        fs::create_directories(dir);
        session_file = dir / "session.toml";
        ui->jobs = &jobs;
        ui->player = &player;
        ui->session = std::make_shared<SessionStore>(session_file);
        ui->first_run_start = fresh;
        shell.emplace(make_shell(*ui, make_app_screens(*ui, models), [] {}), w, h);
    }

    std::string frame() { return shell->frame(); }
    int tab() { return active_tab(shell->root); }
};

bool shows(const std::string& frame, const std::string& text) { return frame.find(text) != std::string::npos; }

std::string start_title() {
    auto title = signals::tr("tui.start.title");
    const auto at = title.find("{version}");
    if (at != std::string::npos) title.replace(at, 9, signals::core::version());
    return title;
}

}  // namespace

TEST_CASE("start panel shows on a fresh session", "[tui][start]") {
    StartFixture f{true};
    const auto frame = f.frame();
    CHECK(shows(frame, start_title()));
    CHECK(shows(frame, signals::tr("tui.start.example")));
    CHECK(shows(frame, signals::tr("tui.start.learn")));
    CHECK(shows(frame, signals::tr("tui.start.open")));
}

TEST_CASE("start panel stays closed for a known session", "[tui][start]") {
    StartFixture f{false};
    CHECK_FALSE(shows(f.frame(), start_title()));
}

TEST_CASE("start panel waits for the terminal check", "[tui][start]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    ui->first_run_check = true;
    ui->first_run_start = true;
    auto models = make_app_models();
    Harness shell{make_shell(*ui, make_app_screens(*ui, models), [] {}), 150, 44};
    CHECK(shows(shell.frame(), signals::tr("tui.check.title")));
    CHECK_FALSE(shows(shell.frame(), start_title()));
    shell.send(ftxui::Event::Escape);
    CHECK(shows(shell.frame(), start_title()));
}

TEST_CASE("item one opens and runs the first steps example", "[tui][start]") {
    StartFixture f{true};
    f.frame();
    f.shell->send(ftxui::Event::Character('1'));
    for (int k = 0; k < 2000 && f.models.pipeline->run_view().running; ++k) std::this_thread::sleep_for(5ms);
    const auto frame = f.frame();
    CHECK(f.tab() == 7);
    CHECK(f.models.pipeline->doc().name == "first_steps");
    CHECK_FALSE(shows(frame, start_title()));
    CHECK(shows(frame, "noisy"));
    CHECK(fs::exists(f.session_file));
}

TEST_CASE("item two opens task 0.1", "[tui][start]") {
    StartFixture f{true};
    f.frame();
    f.shell->send(ftxui::Event::Character('2'));
    const auto frame = f.frame();
    CHECK(f.tab() == 8);
    const auto& course = f.models.learn->course();
    REQUIRE_FALSE(course.chapters.empty());
    REQUIRE_FALSE(course.chapters.front().tasks.empty());
    const auto& first = course.chapters.front().tasks.front();
    CHECK(first.id == "0.1");
    CHECK(shows(frame, first.text().title));
    CHECK_FALSE(shows(frame, start_title()));
}

TEST_CASE("item three asks for an audio file", "[tui][start]") {
    StartFixture f{true};
    f.frame();
    f.shell->send(ftxui::Event::Character('3'));
    const auto frame = f.frame();
    CHECK(f.tab() == 0);
    CHECK(shows(frame, signals::tr("tui.cmd.open_prompt")));
}

TEST_CASE("escape closes the panel and saves the session", "[tui][start]") {
    StartFixture f{true};
    f.frame();
    CHECK_FALSE(fs::exists(f.session_file));
    f.shell->send(ftxui::Event::Escape);
    CHECK_FALSE(shows(f.frame(), start_title()));
    CHECK(fs::exists(f.session_file));
    CHECK_FALSE(SessionStore{f.session_file}.fresh());
}

TEST_CASE("arrows and enter pick an item", "[tui][start]") {
    StartFixture f{true};
    f.frame();
    f.shell->send(ftxui::Event::ArrowDown);
    f.shell->send(ftxui::Event::ArrowDown);
    f.shell->send(ftxui::Event::Return);
    CHECK(f.tab() == 0);
    CHECK(shows(f.frame(), signals::tr("tui.cmd.open_prompt")));
}

TEST_CASE("mouse click picks an item", "[tui][start]") {
    StartFixture f{true};
    const auto frame = f.frame();
    const auto at = signals::test::locate(frame, signals::tr("tui.start.learn"));
    REQUIRE(at);
    f.shell->click(at->first + 1, at->second);
    CHECK(f.tab() == 8);
}

TEST_CASE("palette reopens the start panel", "[tui][start]") {
    StartFixture f{false};
    f.frame();
    f.shell->send(ftxui::Event::Character(':'));
    for (char c : std::string{"start"}) f.shell->send(ftxui::Event::Character(c));
    f.shell->send(ftxui::Event::Return);
    CHECK(shows(f.frame(), start_title()));
}

TEST_CASE("start panel fits 80x24 in ascii without colour", "[tui][start]") {
    StartFixture f{true, 80, 24, GlyphMode::ascii, ColorDepth::none};
    const auto frame = f.frame();
    CHECK(shows(frame, start_title()));
    for (const char* key : {"tui.start.example", "tui.start.learn", "tui.start.open", "tui.start.keys"}) {
        INFO(key);
        CHECK(shows(frame, signals::tr(key)));
    }
}

TEST_CASE("route names list every navigation target", "[tui][shell][routes]") {
    const std::vector<std::pair<std::string_view, int>> want{{"signal", 0},      {"spectrum", 1},      {"resample", 1}, {"tempo", 1},    {"filters", 2},
                                                             {"designer", 3},    {"diagram", 3},       {"decomposition", 4}, {"processing", 6}, {"pipeline", 7},
                                                             {"matrix", 5},      {"explain", 8},       {"under_the_hood", 8}, {"tutorial", 8}, {"learn", 8}};
    const auto names = route_names();
    REQUIRE(names.size() == want.size());
    StartFixture f{false};
    f.frame();
    for (std::size_t i = 0; i < want.size(); ++i) {
        CHECK(names[i] == want[i].first);
        f.ui->navigate(names[i], {});
        CHECK(f.tab() == want[i].second);
    }
}
