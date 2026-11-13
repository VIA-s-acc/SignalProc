#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include <ftxui/component/event.hpp>

#include "learn/checker.hpp"
#include "learn/progress.hpp"
#include "support/paths.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/harness.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/learn_screen.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals;
using namespace signals::tui;
using signals::test::Harness;

namespace {

std::string two(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

learn::FileMap mini_files() { return *learn::read_course_dir(test::learn_fixture("mini")); }

const learn::Course& mini() {
    static const learn::Course c = *learn::load_course(mini_files());
    return c;
}

const learn::Course& long_course() {
    static const learn::Course c = [] {
        auto files = mini_files();
        learn::FileMap out{{"course.toml", files.at("course.toml")}};
        for (int ch = 1; ch <= 3; ++ch) {
            const auto dir = two(ch) + "-c";
            out[dir + "/chapter.toml"] = files.at("01-basics/chapter.toml");
            for (int t = 1; t <= 12; ++t)
                for (const char* f : {"task.toml", "ru.md", "en.md"})
                    out[dir + "/" + two(t) + "-t/" + f] = files.at(std::string{"01-basics/01-sum/"} + f);
        }
        return *learn::load_course(out);
    }();
    return c;
}

struct Fixture {
    std::filesystem::path dir;
    std::shared_ptr<UiContext> ui;
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<PipelineModel> pipeline = std::make_shared<PipelineModel>();
    std::vector<std::filesystem::path> opened;
    std::vector<std::string> routes;
    std::vector<Command> choices;
    std::shared_ptr<TabScreen> screen;

    Fixture(const std::string& name, int w, int h, const learn::Course& course = mini()) : dir{test::temp_path(name)}, ui{test::make_ui(w, h)} {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        ui->jobs = &jobs;
        ui->open_graph = [this](const std::filesystem::path& p) {
            opened.push_back(p);
            (void)pipeline->load(p);
        };
        ui->navigate = [this](std::string_view target, const NavArgs&) { routes.emplace_back(target); };
        ui->choose = [this](std::string, std::vector<Command> items) { choices = std::move(items); };
        ui->learn = std::make_shared<LearnState>(course, dir, pipeline);
        screen = make_learn_screen(*ui);
    }

    void type(const std::string& s) const {
        for (char c : s) screen->OnEvent(ftxui::Event::Character(c));
    }
};

}  // namespace

TEST_CASE("tasks snapshots", "[tui][learn_screen][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        Fixture f{"learn-screen-snap", w, h};
        f.ui->learn->select("1.2");
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("learn_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("selecting a task and starting it opens the pipeline with the work graph", "[tui][learn_screen]") {
    Fixture f{"learn-screen-start", 150, 41};
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::ArrowDown);
    h.frame();
    h.send(ftxui::Event::Return);
    CHECK(f.ui->learn->view().progress.active == "1.2");
    REQUIRE(f.opened.size() == 1);
    CHECK(f.opened[0] == learn::work_path(f.dir, "1.2"));
    h.send(ftxui::Event::ArrowUp);
    h.send(ftxui::Event::Return);
    CHECK(f.ui->learn->view().progress.active == "1.1");
    CHECK(f.routes == std::vector<std::string>{"signal"});
}

TEST_CASE("an answer task checks the typed answer", "[tui][learn_screen]") {
    Fixture f{"learn-screen-answer", 150, 41};
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::Return);
    h.send(ftxui::Event::Character('a'));
    CHECK(f.screen->capturing_text());
    const auto a = std::stoi(learn::draw_params(mini().find("1.1")->params, "1.1", 1).at("a"));
    f.type(std::to_string(a + 1));
    const auto typing = h.frame();
    CHECK(typing.find(std::to_string(a + 1)) != std::string::npos);
    h.send(ftxui::Event::Return);
    CHECK_FALSE(f.screen->capturing_text());
    const auto v = f.ui->learn->view();
    REQUIRE(v.last.has_value());
    CHECK(v.last->passed);
    signals::test::check_snapshot("learn_answer_150x44", Harness{make_shell(*f.ui, {f.screen}, [] {}), 150, 44}.frame());
}

TEST_CASE("hints appear one by one", "[tui][learn_screen]") {
    Fixture f{"learn-screen-hints", 150, 41};
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::Return);
    CHECK(h.frame().find("Прибавь единицу") == std::string::npos);
    h.send(ftxui::Event::Character('h'));
    auto frame = h.frame();
    CHECK(frame.find("Прибавь единицу") != std::string::npos);
    CHECK(frame.find("Получится") == std::string::npos);
    h.send(ftxui::Event::Character('h'));
    CHECK(h.frame().find("Получится") != std::string::npos);
}

TEST_CASE("the solution asks for confirmation before solving", "[tui][learn_screen]") {
    Fixture f{"learn-screen-solution", 150, 41};
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::Return);
    h.send(ftxui::Event::Character('s'));
    REQUIRE_FALSE(f.choices.empty());
    CHECK(f.ui->learn->view().progress.tasks["1.1"].state == learn::TaskState::open);
    f.choices.front().run();
    CHECK(f.ui->learn->view().progress.tasks.at("1.1").state == learn::TaskState::revealed);
    const auto a = std::stoi(learn::draw_params(mini().find("1.1")->params, "1.1", 1).at("a"));
    CHECK(h.frame().find(std::to_string(a) + " + 1 = " + std::to_string(a + 1)) != std::string::npos);
}

TEST_CASE("the task list scrolls with more-markers and follows the cursor", "[tui][learn_screen]") {
    Fixture f{"learn-screen-scroll", 80, 24, long_course()};
    Harness h{f.screen, 80, 21};
    auto frame = h.frame();
    CHECK(frame.find("▼") != std::string::npos);
    for (int i = 0; i < 30; ++i) h.send(ftxui::Event::ArrowDown);
    frame = h.frame();
    CHECK(frame.find("▲") != std::string::npos);
    CHECK(frame.find("3.7") != std::string::npos);
}

TEST_CASE("tab moves focus and z maximizes the focused panel", "[tui][learn_screen]") {
    Fixture f{"learn-screen-tab", 150, 41};
    Harness h{f.screen, 150, 41};
    const auto first = h.frame();
    h.send(ftxui::Event::Character('z'));
    const auto list = h.frame();
    CHECK(list != first);
    h.send(ftxui::Event::Tab);
    const auto text = h.frame();
    CHECK(text != list);
    h.send(ftxui::Event::TabReverse);
    CHECK(h.frame() == list);
}
