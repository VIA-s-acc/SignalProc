#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ftxui/component/event.hpp>

#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/screens/pipeline_screen.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/text_util.hpp"

using namespace signals;
using namespace signals::tui;
using namespace std::chrono_literals;

namespace {

std::string filled(std::string text, const std::string& script, const std::string& data) {
    for (const auto& [key, value] : {std::pair<std::string, std::string>{"{script}", script}, {"{data}", data}})
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key)) text.replace(at, key.size(), value);
    return text;
}

constexpr const char* kGraph = "pipeline \"t\" v2\n"
                               "node x = samples(values=\"0.125 -0.5 0.25 1\", rate=8)\n"
                               "node g = gain(db=-6)\n"
                               "x -> g\n"
                               "probe g as \"g\"\n";

struct Fixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<PipelineModel> model = std::make_shared<PipelineModel>();
    std::shared_ptr<PipelineScreen> screen;
    signals::test::Harness h;

    explicit Fixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)}, screen{(ui->jobs = &jobs, std::make_shared<PipelineScreen>(*ui, model))}, h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        REQUIRE(model->load_text("t", kGraph));
        model->run(jobs);
        for (int k = 0; k < 2000 && model->run_view().running; ++k) std::this_thread::sleep_for(5ms);
        h.frame();
    }
};

}  // namespace

TEST_CASE("P asks where to write and writes script and data", "[tui-python]") {
    Fixture f;
    const auto dir = signals::test::temp_path("tui-python");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string prompt, prefill, answer = (dir / "t.py").string();
    f.ui->ask_text = [&](std::string p, std::string initial, std::function<void(std::string)> done) {
        prompt = std::move(p);
        prefill = std::move(initial);
        done(answer);
    };
    f.h.send(ftxui::Event::Character('P'));
    f.h.frame();
    CHECK(prompt == tr("tui.pipeline.python.prompt"));
    CHECK(prefill == "t.py");
    CHECK(std::filesystem::exists(dir / "t.py"));
    CHECK(std::filesystem::exists(dir / "t.npz"));
    CHECK(f.screen->notice().find("t.py") != std::string::npos);
    CHECK(f.screen->notice().find("t.npz") != std::string::npos);

    answer = (dir / "t.ipynb").string();
    f.h.send(ftxui::Event::Character('P'));
    f.h.frame();
    CHECK(std::filesystem::exists(dir / "t.ipynb"));
    CHECK(f.screen->notice().find("Jupyter") != std::string::npos);

    answer = (dir / "math.py").string();
    f.h.send(ftxui::Event::Character('P'));
    f.h.frame();
    CHECK_FALSE(std::filesystem::exists(dir / "math.py"));
    CHECK(f.screen->notice().find("math.py") != std::string::npos);
}

TEST_CASE("the palette offers the python export", "[tui-python]") {
    Fixture f;
    bool found = false;
    for (const auto& c : f.screen->commands())
        if (c.id == "pipeline.python") found = !c.title.empty();
    CHECK(found);
}

TEST_CASE("inspector shows the python line of the selected node", "[tui-python]") {
    Fixture f;
    f.model->selection = {Selection::Kind::node, "g", {}};
    const auto text = f.h.frame();
    CHECK(text.find(tr("tui.pipeline.python_line")) != std::string::npos);
    CHECK(text.find("y = ") != std::string::npos);
}

TEST_CASE("python export messages fit 80 columns", "[tui-python]") {
    for (const auto lang : {Language::ru, Language::en}) {
        set_language(lang);
        for (const auto& line : {tr("tui.pipeline.python.prompt"), tr("tui.pipeline.cmd.python"), tr("tui.pipeline.hint.python"), tr("tui.pipeline.python_line") + ": y = x",
                                 filled(tr("python.written"), "first_steps.py", "first_steps.npz"), filled(tr("python.written_notebook"), "first_steps.ipynb", "first_steps.npz")}) {
            INFO(line);
            CHECK(display_width(line) <= 80);
        }
    }
    set_language(Language::ru);
}
