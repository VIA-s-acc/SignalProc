#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <ftxui/component/event.hpp>

#include "i18n/i18n.hpp"
#include "tui/harness.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/screens/pipeline_screen.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kGraph = "pipeline \"vars\" v2\n"
                               "let lv = 2\n"
                               "let fs = 8000\n"
                               "node x = gen(kind=sine, freq=440, rate=$fs, duration=0.05)\n"
                               "node d = dwt(wavelet=db2, levels=$lv)\n"
                               "x -> d\n"
                               "probe d.a as \"a\"\n";

struct Fixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<PipelineModel> model = std::make_shared<PipelineModel>();
    std::shared_ptr<PipelineScreen> screen;
    signals::test::Harness h;
    std::string prompt, prefill, answer;

    explicit Fixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)}, screen{(ui->jobs = &jobs, std::make_shared<PipelineScreen>(*ui, model))}, h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        REQUIRE(model->load_text("vars", kGraph));
        ui->ask_text = [this](std::string p, std::string initial, std::function<void(std::string)> done) {
            prompt = std::move(p);
            prefill = std::move(initial);
            done(answer);
        };
        h.frame();
    }
    void focus_vars() {
        for (int k = 0; k < 3; ++k) h.send(ftxui::Event::Tab);
        h.frame();
    }
};

}  // namespace

TEST_CASE("the model edits, adds and removes variables", "[tui-vars]") {
    Fixture f;
    CHECK(f.model->uses_of("lv") == std::vector<std::string>{"d"});
    CHECK(f.model->uses_of("fs") == std::vector<std::string>{"x"});
    REQUIRE(f.model->set_variable("lv", "3"));
    CHECK(f.model->graph()->nodes.at(1).params.at("levels") == "3");
    CHECK_FALSE(f.model->add_variable("lv", "1"));
    REQUIRE(f.model->add_variable("gain", "0.5"));
    CHECK(f.model->doc().lets.size() == 3);
    const auto used = f.model->remove_variable("lv");
    REQUIRE_FALSE(used);
    CHECK_THAT(used.error().message, ContainsSubstring("d"));
    REQUIRE(f.model->remove_variable("gain"));
    CHECK(f.model->doc().lets.size() == 2);
    REQUIRE(f.model->undo());
    CHECK(f.model->doc().lets.size() == 3);
}

TEST_CASE("Enter on a variable asks for its value and reruns", "[tui-vars]") {
    Fixture f;
    f.focus_vars();
    f.answer = "4";
    f.h.send(ftxui::Event::Return);
    CHECK_THAT(f.prompt, ContainsSubstring("lv"));
    CHECK(f.prefill == "2");
    CHECK(f.model->doc().lets.at(0).value == "4");
}

TEST_CASE("plus adds a variable and Delete refuses a used one", "[tui-vars]") {
    Fixture f;
    f.focus_vars();
    f.answer = "half = \"$fs/2\"";
    f.h.send(ftxui::Event::Character('+'));
    REQUIRE(f.model->doc().lets.size() == 3);
    CHECK(f.model->doc().lets.at(2).name == "half");
    CHECK(f.model->doc().lets.at(2).value == "$fs/2");
    f.h.send(ftxui::Event::Delete);
    f.h.frame();
    CHECK_THAT(f.screen->notice(), ContainsSubstring("d"));
    CHECK(f.model->doc().lets.size() == 3);
}

TEST_CASE("the inspector shows a variable and its value", "[tui-vars]") {
    Fixture f;
    f.model->selection = {Selection::Kind::node, "d", {}};
    const auto frame = f.h.frame();
    CHECK_THAT(frame, ContainsSubstring("$lv → 2"));
}

TEST_CASE("the variables panel lists names, text and values", "[tui-vars]") {
    Fixture f;
    const auto frame = f.h.frame();
    CHECK_THAT(frame, ContainsSubstring(signals::tr("tui.pipeline.vars.title").substr(0, 10)));
    CHECK_THAT(frame, ContainsSubstring("fs = 8000"));
}

TEST_CASE("S on a variable starts a sweep over it", "[tui-vars]") {
    Fixture f;
    f.focus_vars();
    f.answer = "";
    f.h.send(ftxui::Event::Character('S'));
    CHECK_THAT(f.prompt, ContainsSubstring("lv"));
    CHECK_FALSE(f.prompt.find(".lv") != std::string::npos);
}

TEST_CASE("the variables panel fits 80x24 and z shows it whole", "[tui-vars]") {
    Fixture f{80, 24};
    for (int k = 0; k < 3; ++k) f.h.send(ftxui::Event::Tab);
    auto frame = f.h.frame();
    CHECK_THAT(frame, ContainsSubstring("lv = 2"));
    f.h.send(ftxui::Event::Character('z'));
    frame = f.h.frame();
    CHECK_THAT(frame, ContainsSubstring("fs = 8000"));
}

TEST_CASE("arrows keep a param bound to a variable and Enter edits its text", "[tui-vars]") {
    Fixture f;
    f.model->selection = {Selection::Kind::node, "d", {}};
    f.h.frame();
    f.h.send(ftxui::Event::Return);
    const auto& params = f.model->graph()->nodes.at(1).type->params;
    const auto levels = std::find_if(params.begin(), params.end(), [](const auto& p) { return p.name == "levels"; }) - params.begin();
    for (long k = 0; k < levels; ++k) f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowRight);
    f.h.frame();
    CHECK_THAT(f.screen->notice(), ContainsSubstring("$lv"));
    CHECK(f.model->doc().nodes.at(1).params.at("levels") == "$lv");
    f.answer = "$lv";
    f.h.send(ftxui::Event::Return);
    CHECK(f.prefill == "$lv");
    CHECK(f.model->doc().nodes.at(1).params.at("levels") == "$lv");
}

TEST_CASE("a file path with a dollar stays literal in a new graph", "[tui-vars]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    auto model = std::make_shared<PipelineModel>();
    const std::filesystem::path file = "/tmp/take$1.wav";
    auto screen = std::make_shared<PipelineScreen>(*ui, model, [file] { return std::optional{file}; });
    signals::test::Harness h{screen, 150, 41};
    h.frame();
    REQUIRE(model->graph());
    REQUIRE(model->graph()->nodes.size() == 1);
    CHECK(model->graph()->nodes.at(0).params.at("path") == file.string());
}

TEST_CASE("the help lists the variables panel keys until the panel has focus", "[tui-vars]") {
    Fixture f;
    const auto titles = [&] {
        std::vector<std::string> out;
        for (const auto& g : f.screen->mode_hints()) out.push_back(g.title);
        return out;
    };
    const auto vars = signals::tr("tui.pipeline.keys.vars");
    const auto before = titles();
    CHECK(std::ranges::find(before, vars) != before.end());
    f.focus_vars();
    const auto after = titles();
    CHECK(std::ranges::find(after, vars) == after.end());
    bool plus = false;
    for (const auto& h : f.screen->hints()) plus = plus || h.key == "+";
    CHECK(plus);
}
