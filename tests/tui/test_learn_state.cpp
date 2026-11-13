#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "learn/checker.hpp"
#include "learn/progress.hpp"
#include "support/paths.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals;
using namespace signals::tui;

namespace {

const learn::Course& mini() {
    static const learn::Course c = *learn::load_course(*learn::read_course_dir(test::learn_fixture("mini")));
    return c;
}

std::filesystem::path fresh(const std::string& name) {
    const auto dir = test::temp_path(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string read(const std::filesystem::path& p) {
    std::ifstream in{p};
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

struct Fixture {
    std::filesystem::path dir;
    std::shared_ptr<UiContext> ui = test::make_ui(150, 44);
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<PipelineModel> pipeline = std::make_shared<PipelineModel>();
    std::shared_ptr<LearnState> learn;
    std::vector<std::filesystem::path> opened;
    std::vector<std::string> routes;

    explicit Fixture(const std::string& name) : dir{fresh(name)} {
        ui->jobs = &jobs;
        ui->open_graph = [this](const std::filesystem::path& p) {
            opened.push_back(p);
            (void)pipeline->load(p);
        };
        ui->navigate = [this](std::string_view target, const NavArgs&) { routes.emplace_back(target); };
        learn = std::make_shared<LearnState>(mini(), dir, pipeline);
        pipeline->finish_listener = [this](const FinishedRun& f) { learn->on_run_finished(f, *ui); };
    }
};

}  // namespace

TEST_CASE("start writes the work graph once and keeps the student's edits", "[tui][learn_state]") {
    Fixture f{"learn-state-start"};
    f.learn->select("1.2");
    CHECK(f.learn->view().progress.active == "1.2");
    f.learn->start(*f.ui);
    const auto work = learn::work_path(f.dir, "1.2");
    REQUIRE(std::filesystem::exists(work));
    CHECK(read(work) == learn::start_graph(*mini().find("1.2"), 1));
    REQUIRE(f.opened.size() == 1);
    CHECK(f.opened[0] == work);
    std::ofstream{work} << learn::solution_graph(*mini().find("1.2"), 1);
    f.learn->start(*f.ui);
    CHECK(read(work) == learn::solution_graph(*mini().find("1.2"), 1));
    f.learn->select("1.1");
    f.learn->start(*f.ui);
    CHECK(f.routes == std::vector<std::string>{"signal"});
}

TEST_CASE("a pipeline run of the work graph triggers a check", "[tui][learn_state]") {
    Fixture f{"learn-state-auto"};
    f.learn->select("1.2");
    f.learn->start(*f.ui);
    std::ofstream{learn::work_path(f.dir, "1.2")} << learn::solution_graph(*mini().find("1.2"), 1);
    REQUIRE(f.pipeline->load(learn::work_path(f.dir, "1.2")).has_value());
    f.pipeline->run(f.jobs);
    const auto v = f.learn->view();
    REQUIRE(v.last.has_value());
    CHECK(v.last->passed);
    CHECK(v.progress.tasks.at("1.2").state == learn::TaskState::solved);
    CHECK(std::filesystem::exists(learn::solution_path(f.dir, "1.2")));
    const auto again = learn::load_progress(learn::progress_path(f.dir));
    CHECK(again.progress.tasks.at("1.2").state == learn::TaskState::solved);
}

TEST_CASE("a run of another graph does not check the task", "[tui][learn_state]") {
    Fixture f{"learn-state-other"};
    f.learn->select("1.2");
    const auto other = f.dir / "other.sig";
    std::ofstream{other} << learn::solution_graph(*mini().find("1.2"), 1);
    REQUIRE(f.pipeline->load(other).has_value());
    f.pipeline->run(f.jobs);
    CHECK_FALSE(f.learn->view().last.has_value());
}

TEST_CASE("F2 checks the open work graph and the file otherwise", "[tui][learn_state]") {
    Fixture f{"learn-state-check"};
    f.learn->select("1.2");
    f.learn->check(*f.ui);
    CHECK_FALSE(f.learn->view().last.has_value());
    CHECK(f.learn->view().message.has_value());
    f.learn->start(*f.ui);
    f.learn->check(*f.ui);
    REQUIRE(f.learn->view().last.has_value());
    CHECK_FALSE(f.learn->view().last->passed);
    REQUIRE(f.pipeline->set_param("y", "db", "6").has_value());
    f.learn->check(*f.ui);
    CHECK(f.learn->view().progress.tasks.at("1.2").attempts == 2);
}

TEST_CASE("hints and reveal update progress", "[tui][learn_state]") {
    Fixture f{"learn-state-hints"};
    f.learn->select("1.1");
    CHECK(f.learn->next_hint());
    CHECK(f.learn->next_hint());
    CHECK_FALSE(f.learn->next_hint());
    CHECK(f.learn->view().progress.tasks.at("1.1").hints == 2);
    f.learn->reveal();
    CHECK(f.learn->view().progress.tasks.at("1.1").state == learn::TaskState::revealed);
    CHECK(learn::load_progress(learn::progress_path(f.dir)).progress.tasks.at("1.1").hints == 2);
}

TEST_CASE("answers are checked", "[tui][learn_state]") {
    Fixture f{"learn-state-answer"};
    f.learn->select("1.1");
    const auto a = std::stoi(learn::draw_params(mini().find("1.1")->params, "1.1", 1).at("a"));
    f.learn->submit_answer(*f.ui, std::to_string(a + 3));
    REQUIRE(f.learn->view().last.has_value());
    CHECK_FALSE(f.learn->view().last->passed);
    f.learn->submit_answer(*f.ui, std::to_string(a + 1));
    CHECK(f.learn->view().last->passed);
    CHECK(f.learn->view().progress.tasks.at("1.1").state == learn::TaskState::solved);
    f.learn->set_variant(9);
    CHECK(f.learn->view().progress.variant == 9);
    CHECK_FALSE(f.learn->view().last.has_value());
}

TEST_CASE("a broken progress file gives a warning, not a crash", "[tui][learn_state]") {
    const auto dir = fresh("learn-state-broken");
    std::ofstream{learn::progress_path(dir)} << "variant = = 2\n";
    LearnState s{mini(), dir, nullptr};
    CHECK(s.view().warning.has_value());
    CHECK(s.view().progress.variant == 1);
}
