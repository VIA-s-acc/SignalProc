#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>

#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/screens/pipeline_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals;
using namespace signals::tui;
using signals::test::Harness;
using Catch::Matchers::ContainsSubstring;
using namespace std::chrono_literals;

namespace {

pipeline::PortRef port(std::string node, std::string name = {}) { return {std::move(node), std::move(name), 0, 0}; }

struct PipelineFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<PipelineModel> model = std::make_shared<PipelineModel>();
    std::shared_ptr<PipelineScreen> screen;
    Harness h;

    explicit PipelineFixture(int w = 150, int hgt = 44, bool denoise = true)
        : ui{signals::test::make_ui(w, hgt)}, screen{(ui->jobs = &jobs, std::make_shared<PipelineScreen>(*ui, model))}, h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        if (denoise) REQUIRE(model->load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
        h.frame();
    }
    ftxui::Box box(std::string_view id) {
        h.frame();
        const auto b = screen->area(id);
        REQUIRE(b.x_max >= b.x_min);
        return b;
    }
};

}  // namespace

TEST_CASE("pipeline snapshots", "[tui][pipeline_screen][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        PipelineFixture f{w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("pipeline_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("arrows walk the graph by layers and e picks the wires of a node", "[tui][pipeline_screen]") {
    PipelineFixture f;
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.model->selection.node == "src");
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.model->selection.node == "dwt");
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::ArrowRight);
    const auto last = f.model->selection.node;
    CHECK((last == "out" || last == "chk"));
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowUp);
    f.h.send(ftxui::Event::ArrowDown);
    CHECK(f.model->selection.node != last);
    f.h.send(ftxui::Event::ArrowLeft);
    CHECK(f.model->selection.node == "idwt");
    f.h.send(ftxui::Event::Character('e'));
    REQUIRE(f.model->selection.kind == Selection::Kind::wire);
    const auto& w = f.model->selection.wire;
    CHECK((w.from.node == "idwt" || w.to.node == "idwt"));
}

TEST_CASE("dragging from an output to an input connects, a wrong type is refused", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    f.model->add_node("dwt");
    f.model->add_node("idwt");
    f.model->add_node("wav_out");
    REQUIRE(f.model->connect(port("gen"), port("dwt")));
    auto from = f.box("out:dwt.coeffs");
    auto to = f.box("in:idwt.in");
    f.h.drag(from.x_min, from.y_min, to.x_min, to.y_min);
    CHECK(f.model->doc().edges.size() == 2);
    from = f.box("out:dwt.coeffs");
    to = f.box("in:wav_out.in");
    f.h.drag(from.x_min, from.y_min, to.x_min, to.y_min);
    CHECK(f.model->doc().edges.size() == 2);
    CHECK_THAT(f.screen->notice(), ContainsSubstring("idwt"));
}

TEST_CASE("a long chain scrolls and keeps the selected node in view", "[tui][pipeline_screen]") {
    PipelineFixture f{80, 24, false};
    f.model->add_node("gen");
    std::string prev = "gen";
    for (int i = 0; i < 39; ++i) {
        const auto id = f.model->add_node("gain");
        REQUIRE(f.model->connect(port(prev), port(id)));
        prev = id;
    }
    auto frame = f.h.frame();
    CHECK(frame.find("▸") != std::string::npos);
    for (int i = 0; i < 40; ++i) f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.model->selection.node == prev);
    frame = f.h.frame();
    CHECK(frame.find(prev) != std::string::npos);
    CHECK(frame.find("◂") != std::string::npos);
}

TEST_CASE("dragging empty space pans and L restores the view", "[tui][pipeline_screen]") {
    PipelineFixture f{80, 24, false};
    f.model->add_node("gen");
    std::string prev = "gen";
    for (int i = 0; i < 10; ++i) {
        const auto id = f.model->add_node("gain");
        REQUIRE(f.model->connect(port(prev), port(id)));
        prev = id;
    }
    const auto area = f.box("graph");
    f.h.drag(area.x_max - 2, area.y_max - 1, area.x_min + 2, area.y_max - 1);
    CHECK(f.screen->view_offset().first > 0);
    f.h.send(ftxui::Event::Character('L'));
    CHECK(f.screen->view_offset().first == 0);
}

TEST_CASE("a finished run marks nodes with their state", "[tui][pipeline_screen]") {
    PipelineFixture f;
    const auto dir = signals::test::temp_path("pipeline-screen-run");
    std::filesystem::create_directories(dir);
    REQUIRE(f.model->set_param("out", "path", (dir / "out.wav").string()));
    f.model->run(f.jobs);
    const auto frame = f.h.frame();
    CHECK(frame.find("✓ dwt") != std::string::npos);
}
