#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "commands/cli.hpp"
#include "core/design/families.hpp"
#include "core/process/shrink.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"
#include "io/wav.hpp"
#include "pipeline/sig.hpp"
#include "support/paths.hpp"
#include "theory/live.hpp"
#include "tui/harness.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/screens/pipeline_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/screen_util.hpp"

using namespace signals;
using namespace signals::tui;
using signals::test::Harness;
using Catch::Matchers::ContainsSubstring;
using namespace std::chrono_literals;
using signals::tr;

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

namespace {

void type(Harness& h, const std::vector<std::string>& chars) {
    for (const auto& c : chars) h.send(ftxui::Event::Character(c));
}

void chain(PipelineFixture& f) {
    f.model->add_node("gen");
    f.model->add_node("dwt");
    f.model->add_node("idwt");
    REQUIRE(f.model->connect(port("gen"), port("dwt")));
    REQUIRE(f.model->connect(port("dwt"), port("idwt")));
    f.model->selection = {Selection::Kind::wire, {}, {port("dwt"), port("idwt")}};
    f.h.frame();
}

}  // namespace

TEST_CASE("a adds a block found by search into the selected wire", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    chain(f);
    CHECK(f.h.send(ftxui::Event::Character('a')));
    CHECK(f.screen->capturing_text());
    type(f.h, {"п", "о", "р"});
    CHECK(f.h.frame().find("Порог") != std::string::npos);
    f.h.send(ftxui::Event::Return);
    CHECK_FALSE(f.screen->capturing_text());
    CHECK_THAT(pipeline::write_sig(f.model->doc()), ContainsSubstring("dwt -> threshold -> idwt"));
}

TEST_CASE("in English the block search finds English names", "[tui][pipeline_screen][i18n]") {
    const signals::LanguageScope en{signals::Language::en};
    PipelineFixture f{150, 44, false};
    chain(f);
    f.h.send(ftxui::Event::Character('a'));
    type(f.h, {"t", "h", "r", "e", "s", "h"});
    f.h.send(ftxui::Event::Return);
    CHECK_THAT(pipeline::write_sig(f.model->doc()), ContainsSubstring("threshold"));
}

TEST_CASE("the inspector edits a choice and Ctrl+Z undoes it", "[tui][pipeline_screen]") {
    PipelineFixture f;
    f.model->selection = {Selection::Kind::node, "thr", {}};
    f.h.send(ftxui::Event::Return);
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowRight);
    const auto& thr = *std::find_if(f.model->doc().nodes.begin(), f.model->doc().nodes.end(), [](const auto& n) { return n.id == "thr"; });
    CHECK(thr.params.at("mode") == "hard");
    f.h.send(ftxui::Event::CtrlZ);
    const auto& back = *std::find_if(f.model->doc().nodes.begin(), f.model->doc().nodes.end(), [](const auto& n) { return n.id == "thr"; });
    CHECK(back.params.at("mode") == "soft");
    f.h.send(ftxui::Event::CtrlY);
    CHECK(f.model->dirty());
}

TEST_CASE("Enter on a text parameter asks for a value", "[tui][pipeline_screen]") {
    PipelineFixture f;
    std::string asked;
    f.ui->ask_text = [&](std::string, std::string initial, std::function<void(std::string)> done) {
        asked = initial;
        done("db4");
    };
    f.model->selection = {Selection::Kind::node, "dwt", {}};
    f.h.send(ftxui::Event::Return);
    f.h.send(ftxui::Event::Return);
    CHECK(asked == "sym8");
    const auto& dwt = *std::find_if(f.model->doc().nodes.begin(), f.model->doc().nodes.end(), [](const auto& n) { return n.id == "dwt"; });
    CHECK(dwt.params.at("wavelet") == "db4");
}

TEST_CASE("M bypasses a block or explains why not", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    f.model->add_node("gain");
    f.model->add_node("dwt");
    REQUIRE(f.model->connect(port("gen"), port("gain")));
    REQUIRE(f.model->connect(port("gain"), port("dwt")));
    f.model->selection = {Selection::Kind::node, "dwt", {}};
    f.h.send(ftxui::Event::Character('M'));
    CHECK_THAT(f.screen->notice(), ContainsSubstring("dwt"));
    f.model->selection = {Selection::Kind::node, "gain", {}};
    f.h.send(ftxui::Event::Character('M'));
    CHECK(f.h.frame().find("┄") != std::string::npos);
    f.h.send(ftxui::Event::Character('p'));
    CHECK(f.model->doc().probes.size() == 1);
    f.h.send(ftxui::Event::Delete);
    CHECK(f.model->doc().nodes.size() == 2);
}

TEST_CASE("Ctrl+S on a new document asks for a path and writes the file", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    const auto path = signals::test::temp_path("pipeline-save") / "new.sig";
    std::filesystem::remove_all(path.parent_path());
    f.ui->ask_text = [&](std::string, std::string, std::function<void(std::string)> done) { done(path.string()); };
    f.h.send(ftxui::Event::CtrlS);
    REQUIRE(std::filesystem::exists(path));
    CHECK_FALSE(f.model->dirty());
}

namespace {

void writable(PipelineFixture& f, const std::string& name) {
    const auto dir = signals::test::temp_path(name);
    std::filesystem::create_directories(dir);
    REQUIRE(f.model->set_param("out", "path", (dir / "out.wav").string()));
}

}  // namespace

TEST_CASE("the probe panel shows the probe and the cache on a rerun", "[tui][pipeline_screen]") {
    PipelineFixture f;
    writable(f, "pipeline-probe");
    f.h.send(ftxui::Event::Character('r'));
    const auto x = io::read_wav(signals::test::sample("test.wav"));
    REQUIRE(x);
    const auto bank = core::wavelet("sym8");
    const auto c = core::wavedec<double>(x->audio.channels[0], *bank, core::default_levels(x->audio.frames(), bank->length()), {});
    REQUIRE(c);
    auto frame = f.h.frame();
    CHECK(frame.find(group(c->bands.back().size())) != std::string::npos);
    f.h.send(ftxui::Event::Character('r'));
    frame = f.h.frame();
    CHECK(frame.find(tr("tui.pipeline.cached")) != std::string::npos);
    f.h.send(ftxui::Event::Character('v'));
    CHECK(f.h.frame().find(tr("tui.pipeline.view.spectrum")) != std::string::npos);
}

TEST_CASE("B plays the probe, A the graph input, both from one position", "[tui][pipeline_screen][audio]") {
    PipelineFixture f;
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    f.ui->player = &player;
    writable(f, "pipeline-ab");
    f.h.send(ftxui::Event::Character('r'));
    f.h.send(ftxui::Event::Character('B'));
    CHECK(player.tag() == "pipeline.probe");
    f.h.send(ftxui::Event::Character('A'));
    CHECK(player.tag() == "pipeline.input");
    f.h.send(ftxui::Event::Character(' '));
    CHECK(player.state() != audio::PlayState::playing);
}

TEST_CASE("the run panel prints the matching CLI command", "[tui][pipeline_screen]") {
    PipelineFixture f;
    REQUIRE(f.model->set_param("thr", "mode", "hard"));
    CHECK(f.h.frame().find("--set thr.mode=hard") != std::string::npos);
}

TEST_CASE("Esc during a run interrupts it", "[tui][pipeline_screen]") {
    JobRunner jobs{[] {}, 0ms};
    auto ui = signals::test::make_ui(150, 44);
    ui->jobs = &jobs;
    auto model = std::make_shared<PipelineModel>();
    model->add_node("gen");
    REQUIRE(model->set_param("gen", "duration", "600"));
    model->add_node("dwt");
    REQUIRE(model->connect(port("gen"), port("dwt")));
    auto screen = std::make_shared<PipelineScreen>(*ui, model);
    Harness h{screen, 150, 41};
    h.send(ftxui::Event::Character('r'));
    std::this_thread::sleep_for(100ms);
    h.send(ftxui::Event::Escape);
    for (int i = 0; i < 400 && model->run_view().running; ++i) std::this_thread::sleep_for(5ms);
    CHECK(h.frame().find(tr("tui.pipeline.cancelled")) != std::string::npos);
}

TEST_CASE("F1 on a node opens the card of its block", "[tui][pipeline_screen][theory]") {
    PipelineFixture f;
    CHECK_FALSE(f.screen->theory_link());
    f.model->selection = {Selection::Kind::node, "thr", {}};
    auto link = f.screen->theory_link();
    REQUIRE(link);
    CHECK(link->kind == TheoryLink::Kind::card);
    CHECK(link->id == "threshold");
    f.model->selection = {Selection::Kind::node, "src", {}};
    CHECK(f.screen->theory_link()->id == "wav_in");
    theory::LiveValues values;
    f.model->selection = {Selection::Kind::node, "dwt", {}};
    f.screen->publish(values);
    REQUIRE(values.find("block.wavelet"));
    CHECK(values.find("block.wavelet")->text == "sym8");
}

TEST_CASE("a pipeline built in the editor writes what the CLI writes", "[tui][pipeline_screen][acceptance]") {
    const auto dir = signals::test::temp_path("pipeline-acceptance");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    PipelineFixture f{150, 44, false};
    auto& m = *f.model;
    m.add_node("wav");
    REQUIRE(m.set_param("wav", "path", (std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav").string()));
    m.add_node("dwt");
    REQUIRE(m.set_param("dwt", "wavelet", "sym8"));
    m.add_node("threshold");
    m.add_node("idwt");
    m.add_node("wav_out");
    REQUIRE(m.set_param("wav_out", "path", (dir / "tui.wav").string()));
    REQUIRE(m.set_param("wav_out", "dither", "none"));
    REQUIRE(m.connect(port("wav"), port("dwt")));
    REQUIRE(m.connect(port("dwt"), port("threshold")));
    REQUIRE(m.connect(port("threshold"), port("idwt")));
    REQUIRE(m.connect(port("idwt"), port("wav_out")));
    f.h.send(ftxui::Event::Character('r'));
    REQUIRE_FALSE(m.run_view().error);
    REQUIRE(m.save(dir / "built.sig"));
    std::ostringstream out, err;
    const auto sig = (dir / "built.sig").string(), cli = (dir / "cli.wav").string();
    const std::vector<std::string_view> args{"signals", "run", sig, "-o", cli};
    REQUIRE(cli::run_cli(args, out, err) == 0);
    const auto bytes = [](const std::filesystem::path& p) {
        std::ifstream in{p, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    CHECK(bytes(dir / "tui.wav") == bytes(cli));
    std::ostringstream out2, err2;
    const auto example = (std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig").string(), cli2 = (dir / "example.wav").string();
    const std::vector<std::string_view> args2{"signals", "run", example, "-o", cli2};
    REQUIRE(cli::run_cli(args2, out2, err2) == 0);
    CHECK(bytes(dir / "tui.wav") == bytes(cli2));
}

TEST_CASE("c connects two ports from the keyboard", "[tui][pipeline_screen][review]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    f.model->add_node("dwt");
    f.model->add_node("idwt");
    REQUIRE(f.model->connect(port("gen"), port("dwt")));
    f.model->selection = {Selection::Kind::node, "dwt", {}};
    f.h.frame();
    f.h.send(ftxui::Event::Character('c'));
    f.h.send(ftxui::Event::ArrowLeft);
    f.h.send(ftxui::Event::ArrowDown);
    CHECK(f.model->selection.node == "idwt");
    f.h.send(ftxui::Event::Return);
    CHECK(f.model->doc().edges.size() == 2);
}

TEST_CASE("save and open prompts expand quoted paths", "[tui][pipeline_screen][review]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    const auto path = signals::test::temp_path("pipeline quoted") / "q.sig";
    std::filesystem::remove_all(path.parent_path());
    std::filesystem::create_directories(path.parent_path());
    f.ui->ask_text = [&](std::string, std::string, std::function<void(std::string)> done) { done("'" + path.string() + "'"); };
    f.h.send(ftxui::Event::CtrlS);
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("a failed run says why", "[tui][pipeline_screen][review]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("wav");
    REQUIRE(f.model->set_param("wav", "path", "no-such.wav"));
    f.h.send(ftxui::Event::Character('r'));
    CHECK(f.h.frame().find("no-such.wav") != std::string::npos);
}

TEST_CASE("Enter opens the inspector in a narrow window too", "[tui][pipeline_screen][review]") {
    PipelineFixture f{80, 24};
    f.model->selection = {Selection::Kind::node, "thr", {}};
    f.h.frame();
    f.h.send(ftxui::Event::Return);
    f.h.frame();
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowRight);
    const auto& thr = *std::find_if(f.model->doc().nodes.begin(), f.model->doc().nodes.end(), [](const auto& n) { return n.id == "thr"; });
    CHECK(thr.params.at("mode") == "hard");
}

namespace {

ftxui::Event left_button(ftxui::Mouse::Motion motion, int x, int y) {
    ftxui::Mouse m;
    m.button = ftxui::Mouse::Left;
    m.motion = motion;
    m.x = x;
    m.y = y;
    return ftxui::Event::Mouse("", m);
}

std::string line_containing(const std::string& frame, const std::string& needle) {
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);)
        if (line.find(needle) != std::string::npos) return line;
    return {};
}

}  // namespace

TEST_CASE("dragging a wire shows it on the way and names both ends", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    f.model->add_node("dwt");
    f.model->add_node("idwt");
    REQUIRE(f.model->connect(port("gen"), port("dwt")));
    const auto from = f.box("out:dwt.coeffs");
    const auto to = f.box("in:idwt.in");
    f.h.send(left_button(ftxui::Mouse::Pressed, from.x_min, from.y_min));
    f.h.send(left_button(ftxui::Mouse::Moved, to.x_min, to.y_min));
    const auto frame = f.h.frame();
    CHECK(frame.find("dwt.coeffs → idwt.in") != std::string::npos);
    CHECK(frame.find("····") != std::string::npos);
    f.h.send(left_button(ftxui::Mouse::Released, to.x_min, to.y_min));
    CHECK(f.model->doc().edges.size() == 2);
    CHECK(f.h.frame().find("dwt.coeffs → idwt.in") == std::string::npos);
}

TEST_CASE("long values in the inspector and the .sig text keep their ends visible", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    const auto id = f.model->add_node("wav");
    const std::string path = "/Users/someone/projects/signals/resources/samples/long-folder-name/test.wav";
    REQUIRE(f.model->set_param(id, "path", path));
    f.model->selection = {Selection::Kind::node, id, {}};
    const auto frame = f.h.frame();
    const auto inspector = line_containing(frame, "path ");
    INFO(inspector);
    CHECK(inspector.find("test.wav") != std::string::npos);
    CHECK(inspector.find("…") != std::string::npos);
    CHECK(frame.find("test.wav\")") != std::string::npos);
}

TEST_CASE("the wavelet parameter is picked from a list, and arrows step through it", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    std::string title;
    std::vector<Command> items;
    f.ui->choose = [&](std::string t, std::vector<Command> list) {
        title = std::move(t);
        items = std::move(list);
    };
    const auto id = f.model->add_node("dwt");
    f.model->selection = {Selection::Kind::node, id, {}};
    f.h.frame();
    const auto inspector = f.screen->area("inspector");
    REQUIRE(inspector.x_max > inspector.x_min);
    f.h.click(inspector.x_min + 2, inspector.y_min + 1);
    f.h.send(ftxui::Event::Return);
    REQUIRE(items.size() > 100);
    CHECK(title.find("wavelet") != std::string::npos);
    const auto sym8 = std::find_if(items.begin(), items.end(), [](const Command& c) { return c.title == "sym8"; });
    REQUIRE(sym8 != items.end());
    sym8->run();
    CHECK(f.model->doc().nodes.back().params.at("wavelet") == "sym8");
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.model->doc().nodes.back().params.at("wavelet") != "sym8");
}

TEST_CASE("the wheel scrolls the pipeline graph, shift scrolls it sideways", "[tui][pipeline_screen]") {
    PipelineFixture f{80, 24, false};
    f.model->add_node("gen");
    for (int k = 0; k < 8; ++k) f.model->add_node("gain");
    f.h.frame();
    const auto graph = f.screen->area("graph");
    REQUIRE(graph.x_max > graph.x_min);
    const auto first = f.box("node:gen");
    ftxui::Mouse m;
    m.button = ftxui::Mouse::WheelDown;
    m.motion = ftxui::Mouse::Pressed;
    m.x = (graph.x_min + graph.x_max) / 2;
    m.y = (graph.y_min + graph.y_max) / 2;
    f.h.send(ftxui::Event::Mouse("", m));
    f.h.send(ftxui::Event::Mouse("", m));
    const auto moved = f.box("node:gen");
    CHECK((moved.y_min < first.y_min || moved.x_max < moved.x_min));
}

TEST_CASE("the convolve filter is picked from a list of bank filters", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    std::vector<Command> items;
    f.ui->choose = [&](std::string, std::vector<Command> list) { items = std::move(list); };
    const auto id = f.model->add_node("convolve");
    f.model->selection = {Selection::Kind::node, id, {}};
    f.h.frame();
    const auto inspector = f.screen->area("inspector");
    f.h.click(inspector.x_min + 2, inspector.y_min + 1);
    f.h.send(ftxui::Event::Return);
    const auto f1 = std::find_if(items.begin(), items.end(), [](const Command& c) { return c.title == "sym4.f1"; });
    REQUIRE(f1 != items.end());
    f1->run();
    CHECK(f.model->doc().nodes.back().params.at("filter") == "sym4.f1");
}

TEST_CASE("the add-block list scrolls with the wheel and adds the clicked block", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    f.h.send(ftxui::Event::Character('a'));
    f.h.frame();
    const auto list = f.screen->area("inspector");
    ftxui::Mouse m;
    m.button = ftxui::Mouse::WheelDown;
    m.motion = ftxui::Mouse::Pressed;
    m.x = (list.x_min + list.x_max) / 2;
    m.y = (list.y_min + list.y_max) / 2;
    const auto last = std::string{signals::pipeline::block_types().back().id};
    CHECK(f.h.frame().find(" " + last + " ") == std::string::npos);
    for (int k = 0; k < 100; ++k) f.h.send(ftxui::Event::Mouse("", m));
    const auto frame = f.h.frame();
    CHECK(frame.find("▲") != std::string::npos);
    const auto row = f.screen->area("add:" + last);
    REQUIRE(row.x_max >= row.x_min);
    f.h.click(row.x_min + 1, row.y_min);
    REQUIRE_FALSE(f.model->doc().nodes.empty());
    CHECK(f.model->doc().nodes.back().type == last);
}

TEST_CASE("C lists the free inputs a selected output can feed and connects the chosen one", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    std::vector<Command> items;
    f.ui->choose = [&](std::string, std::vector<Command> list) { items = std::move(list); };
    f.model->add_node("gen");
    const auto dwt = f.model->add_node("dwt");
    f.model->add_node("idwt");
    f.model->add_node("check");
    REQUIRE(f.model->connect(port("gen"), port("dwt")));
    f.model->selection = {Selection::Kind::node, dwt, {}};
    f.h.frame();
    f.h.send(ftxui::Event::Character('C'));
    const auto it = std::find_if(items.begin(), items.end(), [](const Command& c) { return c.title.find("idwt") != std::string::npos; });
    REQUIRE(it != items.end());
    CHECK(std::none_of(items.begin(), items.end(), [](const Command& c) { return c.title.find("check") != std::string::npos; }));
    it->run();
    CHECK(f.model->doc().edges.size() == 2);
    const auto hints = f.screen->hints();
    CHECK(std::any_of(hints.begin(), hints.end(), [](const signals::tui::Hint& h) { return h.key == "c/C"; }));
}

TEST_CASE("the example command lists the built-in graphs and opens the chosen one", "[tui][pipeline_screen]") {
    PipelineFixture f{150, 44, false};
    std::vector<Command> items;
    f.ui->choose = [&](std::string, std::vector<Command> list) { items = std::move(list); };
    const auto commands = f.screen->commands();
    const auto example = std::find_if(commands.begin(), commands.end(), [](const Command& c) { return c.id == "pipeline.example"; });
    REQUIRE(example != commands.end());
    example->run();
    REQUIRE(items.size() >= 4);
    const auto haar = std::find_if(items.begin(), items.end(), [](const Command& c) { return c.id == "haar_bank"; });
    REQUIRE(haar != items.end());
    CHECK(haar->title.find("Хаар") != std::string::npos);
    haar->run();
    const auto& nodes = f.model->doc().nodes;
    CHECK(std::any_of(nodes.begin(), nodes.end(), [](const auto& n) { return n.type == "add"; }));
    f.model->run(f.jobs);
    for (int k = 0; k < 400 && f.model->run_view().running; ++k) std::this_thread::sleep_for(5ms);
    const auto run = f.model->run_view();
    CHECK_FALSE(run.error);
    REQUIRE_FALSE(run.reports.empty());
    CHECK(run.reports.front().report.passed);
}

TEST_CASE("the .sig text and the run panel scroll with markers and the wheel", "[tui][pipeline_screen][scroll]") {
    PipelineFixture f{150, 44, false};
    f.model->add_node("gen");
    std::string prev = "gen";
    for (int k = 0; k < 18; ++k) {
        const auto id = f.model->add_node("gain");
        REQUIRE(f.model->connect(port(prev), port(id)));
        prev = id;
    }
    f.model->run(f.jobs);
    for (int k = 0; k < 400 && f.model->run_view().running; ++k) std::this_thread::sleep_for(5ms);
    f.model->selection = {};
    auto frame = f.h.frame();
    const auto text = f.screen->area("text_panel");
    const auto run = f.screen->area("run_panel");
    REQUIRE(text.x_max > text.x_min);
    REQUIRE(run.x_max > run.x_min);
    CHECK(frame.find("▼ ещё") != std::string::npos);
    for (const auto box : {text, run}) {
        ftxui::Mouse m;
        m.button = ftxui::Mouse::WheelDown;
        m.motion = ftxui::Mouse::Pressed;
        m.x = (box.x_min + box.x_max) / 2;
        m.y = (box.y_min + box.y_max) / 2;
        for (int k = 0; k < 4; ++k) f.h.send(ftxui::Event::Mouse("", m));
    }
    frame = f.h.frame();
    CHECK(frame.find("▲ ещё") != std::string::npos);
}

TEST_CASE("the probe plots zoom with the wheel and the add list explains the picked block", "[tui][pipeline_screen][scroll]") {
    PipelineFixture f{150, 44, true};
    f.model->run(f.jobs);
    for (int k = 0; k < 2000 && f.model->run_view().running; ++k) std::this_thread::sleep_for(5ms);
    f.h.frame();
    const auto b = f.screen->area("probe_time_plot");
    REQUIRE(b.x_max > b.x_min);
    const auto full = f.screen->plot_view("probe_time");
    REQUIRE(full);
    f.h.wheel(b.x_min + (b.x_max - b.x_min) / 3, (b.y_min + b.y_max) / 2, true);
    f.h.wheel(b.x_min + (b.x_max - b.x_min) / 3, (b.y_min + b.y_max) / 2, true);
    f.h.frame();
    CHECK(f.screen->plot_view("probe_time")->hi - f.screen->plot_view("probe_time")->lo < 0.7 * (full->hi - full->lo));
    CHECK_FALSE(f.screen->context_menu(b.x_min + 2, (b.y_min + b.y_max) / 2).empty());
    f.h.send(ftxui::Event::Character('a'));
    f.h.send(ftxui::Event::Character("idwt"));
    const auto frame = f.h.frame();
    CHECK(frame.find("in: coeffs → out: signal") != std::string::npos);
}

TEST_CASE("the inspector scrolls a long parameter list and follows the picked parameter", "[tui][pipeline_screen][scroll]") {
    PipelineFixture f{80, 24, false};
    const auto types = signals::pipeline::block_types();
    const auto most = std::max_element(types.begin(), types.end(), [](const auto& a, const auto& b) { return a.params.size() < b.params.size(); });
    REQUIRE(most->params.size() >= 6);
    f.ui->terminal_size = [] { return std::pair{80, 14}; };
    const auto id = f.model->add_node(most->id);
    f.model->selection = {Selection::Kind::node, id, {}};
    f.h.frame();
    f.h.send(ftxui::Event::Return);
    auto frame = f.h.frame();
    CHECK(frame.find("▼") != std::string::npos);
    for (std::size_t k = 0; k + 1 < most->params.size(); ++k) f.h.send(ftxui::Event::ArrowDown);
    frame = f.h.frame();
    CHECK(frame.find(most->params.back().name) != std::string::npos);
    CHECK(frame.find("▲") != std::string::npos);
}

TEST_CASE("the wavelet and filter lists offer the user banks", "[tui][pipeline_screen][user_banks]") {
    const auto db2 = signals::core::wavelet("db2");
    REQUIRE(db2);
    signals::core::set_user_wavelets([db2](std::string_view name) -> std::optional<signals::core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db2;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    PipelineFixture f{150, 44, false};
    std::vector<Command> items;
    f.ui->choose = [&](std::string, std::vector<Command> list) { items = std::move(list); };
    for (const auto& [type, wanted] : {std::pair{"dwt", "my_bank"}, std::pair{"convolve", "my_bank.f0"}}) {
        items.clear();
        const auto id = f.model->add_node(type);
        f.model->selection = {Selection::Kind::node, id, {}};
        f.h.frame();
        f.h.send(ftxui::Event::Return);
        const auto* t = signals::pipeline::find_block_type(type);
        const auto param = std::find_if(t->params.begin(), t->params.end(), [](const auto& p) { return p.name == "wavelet" || p.name == "filter"; });
        REQUIRE(param != t->params.end());
        for (auto k = t->params.begin(); k != param; ++k) f.h.send(ftxui::Event::ArrowDown);
        f.h.send(ftxui::Event::Return);
        const auto it = std::find_if(items.begin(), items.end(), [&](const Command& c) { return c.title == wanted; });
        REQUIRE(it != items.end());
        it->run();
        CHECK(f.model->doc().nodes.back().params.at(param->name) == wanted);
        f.h.send(ftxui::Event::Escape);
    }
    signals::core::set_user_wavelets({}, {});
}
