#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "commands/cli.hpp"
#include "pipeline/sig.hpp"
#include "support/paths.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/pipeline_model.hpp"

using namespace signals;
using namespace signals::tui;
using Catch::Matchers::ContainsSubstring;

namespace {

pipeline::PortRef port(std::string node, std::string name = {}) { return {std::move(node), std::move(name), 0, 0}; }

std::filesystem::path fresh_dir(const std::string& name) {
    const auto dir = test::temp_path(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void build_denoise(PipelineModel& m, const std::string& wav_path) {
    REQUIRE(m.add_node("wav") == "wav");
    REQUIRE(m.set_param("wav", "path", wav_path));
    REQUIRE(m.add_node("dwt") == "dwt");
    REQUIRE(m.set_param("dwt", "wavelet", "sym8"));
    REQUIRE(m.add_node("threshold") == "threshold");
    REQUIRE(m.add_node("idwt") == "idwt");
    REQUIRE(m.add_node("check") == "check");
    REQUIRE(m.set_param("check", "kind", "snr"));
    REQUIRE(m.set_param("check", "tol", "0"));
    REQUIRE(m.add_node("wav_out") == "wav_out");
    REQUIRE(m.set_param("wav_out", "dither", "none"));
    REQUIRE(m.connect(port("wav"), port("dwt")));
    REQUIRE(m.connect(port("dwt"), port("threshold")));
    REQUIRE(m.connect(port("threshold"), port("idwt")));
    REQUIRE(m.connect(port("idwt"), port("wav_out")));
    REQUIRE(m.connect(port("wav"), port("check", "ref")));
    REQUIRE(m.connect(port("idwt"), port("check", "in")));
}

}  // namespace

TEST_CASE("the denoise graph can be built from scratch with edits", "[tui][pipeline_model]") {
    PipelineModel m;
    CHECK(m.problem());
    build_denoise(m, "test.wav");
    CHECK_FALSE(m.problem());
    REQUIRE(m.graph());
    CHECK(m.graph()->nodes.size() == 6);
    CHECK(m.doc().edges.size() == 6);
    const auto text = pipeline::write_sig(m.doc());
    const auto again = pipeline::parse_sig(text);
    REQUIRE(again);
    CHECK(pipeline::write_sig(*again) == text);
    CHECK(m.dirty());
}

TEST_CASE("new nodes get unique ids", "[tui][pipeline_model]") {
    PipelineModel m;
    CHECK(m.add_node("dwt") == "dwt");
    CHECK(m.add_node("dwt") == "dwt2");
    CHECK(m.add_node("dwt") == "dwt3");
}

TEST_CASE("a block inserted into a wire takes its place", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("gen");
    m.add_node("dwt");
    m.add_node("idwt");
    REQUIRE(m.connect(port("gen"), port("dwt")));
    REQUIRE(m.connect(port("dwt"), port("idwt")));
    const auto inserted = m.insert_into_wire({port("dwt"), port("idwt")}, "threshold");
    INFO((inserted ? std::string{} : inserted.error().message));
    REQUIRE(inserted);
    const auto text = pipeline::write_sig(m.doc());
    CHECK_THAT(text, ContainsSubstring("dwt -> threshold -> idwt"));
    CHECK_FALSE(m.problem());
}

TEST_CASE("a cycle or a wrong type is refused and changes nothing", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("gen");
    m.add_node("dwt");
    m.add_node("threshold");
    m.add_node("wav_out");
    REQUIRE(m.connect(port("gen"), port("dwt")));
    REQUIRE(m.connect(port("dwt"), port("threshold")));
    const auto before = pipeline::write_sig(m.doc());
    const auto depth = m.undo_depth();
    const auto cycle = m.connect(port("threshold"), port("dwt"));
    REQUIRE_FALSE(cycle);
    const auto type = m.connect(port("threshold"), port("wav_out"));
    REQUIRE_FALSE(type);
    CHECK_THAT(type.error().message, ContainsSubstring("idwt"));
    CHECK(pipeline::write_sig(m.doc()) == before);
    CHECK(m.undo_depth() == depth);
}

TEST_CASE("undo and redo restore documents exactly", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("gen");
    const auto one = pipeline::write_sig(m.doc());
    m.add_node("gain");
    REQUIRE(m.connect(port("gen"), port("gain")));
    REQUIRE(m.set_param("gain", "db", "-6"));
    const auto four = pipeline::write_sig(m.doc());
    CHECK(m.undo());
    CHECK(m.undo());
    CHECK(m.undo());
    CHECK(pipeline::write_sig(m.doc()) == one);
    CHECK(m.redo());
    CHECK(m.redo());
    CHECK(m.redo());
    CHECK(pipeline::write_sig(m.doc()) == four);
    CHECK_FALSE(m.redo());
    REQUIRE(m.toggle_bypass("gain"));
    CHECK_FALSE(m.toggle_bypass("gen"));
    m.toggle_probe(port("gain"));
    CHECK(m.doc().probes.size() == 1);
    m.remove_node("gain");
    CHECK(m.doc().probes.empty());
    CHECK(m.doc().edges.empty());
}

TEST_CASE("save as into another directory rewrites relative paths", "[tui][pipeline_model]") {
    const auto dir = fresh_dir("pipeline-model-save");
    PipelineModel m;
    REQUIRE(m.load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
    CHECK_FALSE(m.dirty());
    const auto target = dir / "deeper" / "копия.sig";
    REQUIRE(m.save(target));
    CHECK_FALSE(m.dirty());
    std::ifstream in{target};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text.find("../resources/samples/test.wav") == std::string::npos);
    std::ostringstream out, err;
    const auto out_path = (dir / "out.wav").string(), sig_path = target.string();
    const std::vector<std::string_view> args{"signals", "run", sig_path, "-o", out_path};
    CHECK(cli::run_cli(args, out, err) == 0);
    INFO(err.str());
}

TEST_CASE("loading over an edited document can be undone", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("gen");
    const auto edited = pipeline::write_sig(m.doc());
    REQUIRE(m.load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
    CHECK(m.doc().nodes.size() == 6);
    CHECK(m.undo());
    CHECK(pipeline::write_sig(m.doc()) == edited);
    const auto bad = fresh_dir("pipeline-model-bad") / "bad.sig";
    std::ofstream{bad} << "node a = gen(\n";
    const auto r = m.load(bad);
    REQUIRE_FALSE(r);
    CHECK_THAT(r.error().message, ContainsSubstring("bad.sig:1"));
}

namespace {

PipelineModel denoise_model(const std::string& name) {
    const auto dir = fresh_dir(name);
    PipelineModel m;
    REQUIRE(m.load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
    REQUIRE(m.set_param("out", "path", (dir / "out.wav").string()));
    REQUIRE(m.save(dir / "denoise.sig"));
    return m;
}

JobRunner inline_jobs() { return JobRunner{[] {}, std::chrono::milliseconds{0}, true}; }

}  // namespace

TEST_CASE("running the editor graph fills node states, reports and the bottleneck", "[tui][pipeline_model]") {
    auto m = denoise_model("model-run");
    auto jobs = inline_jobs();
    m.run(jobs);
    const auto v = m.run_view();
    CHECK_FALSE(v.running);
    CHECK_FALSE(v.error);
    CHECK(v.number == 1);
    REQUIRE(v.nodes.size() == 6);
    int bottlenecks = 0;
    for (const auto& [id, n] : v.nodes) {
        INFO(id);
        CHECK(n.status.state == pipeline::NodeState::done);
        bottlenecks += n.bottleneck ? 1 : 0;
    }
    CHECK(bottlenecks == (v.seconds > 0.05 ? 1 : 0));
    REQUIRE(v.reports.size() == 2);
    CHECK(v.probes.contains("d1"));
    CHECK(m.cache_bytes() > 0);
}

TEST_CASE("after a parameter change only that node and its descendants run again", "[tui][pipeline_model]") {
    auto m = denoise_model("model-rerun");
    auto jobs = inline_jobs();
    m.run(jobs);
    REQUIRE(m.set_param("thr", "mode", "hard"));
    m.run(jobs);
    const auto v = m.run_view();
    CHECK(v.number == 2);
    CHECK(v.nodes.at("src").status.state == pipeline::NodeState::cached);
    CHECK(v.nodes.at("dwt").status.state == pipeline::NodeState::cached);
    CHECK(v.nodes.at("thr").status.state == pipeline::NodeState::done);
    CHECK(v.nodes.at("idwt").status.state == pipeline::NodeState::done);
    CHECK(v.sets == std::vector<std::string>{"thr.mode=hard"});
}

TEST_CASE("editing during a run drops removed nodes and the next run works", "[tui][pipeline_model]") {
    auto m = denoise_model("model-edit");
    auto jobs = inline_jobs();
    bool removed = false;
    m.run_listener = [&](const pipeline::NodeStatus& s) {
        if (!removed && s.id == "src" && s.state == pipeline::NodeState::done) {
            removed = true;
            m.remove_node("chk");
        }
    };
    m.run(jobs);
    CHECK(removed);
    CHECK_FALSE(m.run_view().nodes.contains("chk"));
    m.run_listener = {};
    m.run(jobs);
    const auto v = m.run_view();
    CHECK_FALSE(v.error);
    CHECK(v.nodes.size() == 5);
}

TEST_CASE("a failing block turns red, its descendants are bypassed, and a fix runs", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("wav");
    REQUIRE(m.set_param("wav", "path", "no-such.wav"));
    m.add_node("gain");
    REQUIRE(m.connect(port("wav"), port("gain")));
    auto jobs = inline_jobs();
    m.run(jobs);
    auto v = m.run_view();
    REQUIRE(v.error);
    CHECK(v.nodes.at("wav").status.state == pipeline::NodeState::failed);
    CHECK_FALSE(v.nodes.at("wav").status.message.empty());
    CHECK(v.nodes.at("gain").status.state == pipeline::NodeState::bypassed);
    REQUIRE(m.set_param("wav", "path", (std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav").string()));
    m.run(jobs);
    v = m.run_view();
    CHECK_FALSE(v.error);
    CHECK(v.nodes.at("gain").status.state == pipeline::NodeState::done);
}

TEST_CASE("cancel stops a long editor run", "[tui][pipeline_model]") {
    PipelineModel m;
    m.add_node("gen");
    REQUIRE(m.set_param("gen", "duration", "600"));
    m.add_node("dwt");
    REQUIRE(m.connect(port("gen"), port("dwt")));
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}};
    m.run(jobs);
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    m.cancel(jobs);
    for (int i = 0; i < 400 && m.run_view().running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    const auto v = m.run_view();
    CHECK_FALSE(v.running);
    CHECK(v.cancelled);
    CHECK(v.nodes.at("dwt").status.state != pipeline::NodeState::done);
}

TEST_CASE("undoing an open restores the previous file too", "[tui][pipeline_model][review]") {
    const auto dir = fresh_dir("pipeline-undo-open");
    PipelineModel m;
    m.add_node("gen");
    REQUIRE(m.save(dir / "mine.sig"));
    m.add_node("gain");
    const auto mine = pipeline::write_sig(m.doc());
    REQUIRE(m.load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
    REQUIRE(m.undo());
    CHECK(pipeline::write_sig(m.doc()) == mine);
    REQUIRE(m.path());
    CHECK(m.path()->filename() == "mine.sig");
    CHECK(m.dirty());
}

TEST_CASE("a failed save changes nothing in memory", "[tui][pipeline_model][review]") {
    PipelineModel m;
    REQUIRE(m.load(std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "denoise.sig"));
    const auto before = pipeline::write_sig(m.doc());
    const auto blocked = fresh_dir("pipeline-save-blocked");
    std::filesystem::create_directories(blocked / "taken.sig");
    CHECK_FALSE(m.save(blocked / "taken.sig"));
    CHECK(pipeline::write_sig(m.doc()) == before);
    CHECK(m.path()->filename() == "denoise.sig");
    REQUIRE(m.save(*m.path()));
    CHECK(pipeline::write_sig(m.doc()) == before);
}

TEST_CASE("a run started during a run owns the view", "[tui][pipeline_model][review]") {
    PipelineModel m;
    m.add_node("gen");
    REQUIRE(m.set_param("gen", "duration", "600"));
    m.add_node("dwt");
    REQUIRE(m.connect(port("gen"), port("dwt")));
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}};
    m.run(jobs);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    REQUIRE(m.set_param("gen", "duration", "0.1"));
    m.run(jobs);
    for (int i = 0; i < 1000 && m.run_view().running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    const auto v = m.run_view();
    CHECK(v.number == 2);
    CHECK_FALSE(v.running);
    CHECK_FALSE(v.cancelled);
    CHECK(v.nodes.at("dwt").status.state == pipeline::NodeState::done);
}

TEST_CASE("run views share probe data instead of copying it", "[tui][pipeline_model][review]") {
    auto m = denoise_model("model-share");
    auto jobs = inline_jobs();
    m.run(jobs);
    const auto a = m.run_view(), b = m.run_view();
    REQUIRE(a.probes.contains("d1"));
    CHECK(a.probes.at("d1").get() == b.probes.at("d1").get());
    REQUIRE(a.input);
    CHECK(a.input.get() == b.input.get());
}

TEST_CASE("the CLI line is withheld when the graph structure changed", "[tui][pipeline_model][review]") {
    auto m = denoise_model("model-structure");
    REQUIRE(m.set_param("thr", "mode", "hard"));
    CHECK_FALSE(m.run_view().structure_changed);
    REQUIRE(m.set_param("out", "path", "with space.wav"));
    CHECK(std::find(m.run_view().sets.begin(), m.run_view().sets.end(), "'out.path=with space.wav'") != m.run_view().sets.end());
    REQUIRE(m.toggle_bypass("thr"));
    CHECK(m.run_view().structure_changed);
}

TEST_CASE("a probe takes a name, keeps its last value and an empty name restores the default", "[tui][pipeline_model][probe_name]") {
    PipelineModel m;
    REQUIRE(m.load_text("conv", "pipeline \"conv\" v1\n"
                                "node x = samples(values=\"2 -1 3 1\", rate=1)\n"
                                "node y = fir_taps(taps=\"0.5 -0.5\")\n"
                                "x -> y\n"
                                "probe y\n"
                                "probe x as \"вход\"\n"));
    auto jobs = inline_jobs();
    m.run(jobs);
    for (int k = 0; k < 2000 && m.run_view().running; ++k) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    REQUIRE(m.run_view().probes.contains("y"));
    REQUIRE(m.rename_probe(port("y"), "выход \"y\""));
    CHECK(m.doc().probes[0].name == "выход \"y\"");
    CHECK_THAT(pipeline::write_sig(m.doc()), ContainsSubstring("probe y as \"выход \\\"y\\\"\""));
    CHECK(m.run_view().probes.contains("выход \"y\""));
    CHECK_FALSE(m.run_view().probes.contains("y"));
    CHECK_FALSE(m.rename_probe(port("y"), "вход"));
    CHECK(m.doc().probes[0].name == "выход \"y\"");
    CHECK_FALSE(m.rename_probe(port("missing"), "z"));
    REQUIRE(m.rename_probe(port("y"), ""));
    CHECK(m.doc().probes[0].name.empty());
    CHECK(m.run_view().probes.contains("y"));
    REQUIRE(m.undo());
    CHECK(m.doc().probes[0].name == "выход \"y\"");
}

TEST_CASE("a node is duplicated with its parameters and comments but without wires", "[tui][pipeline_model][probe_tools]") {
    PipelineModel m;
    REQUIRE(m.load_text("g", "pipeline \"g\" v1\n\nnode x = samples(values=\"1 2\", rate=8)\nnode g = gain(db=-6)  # quieter\n\nx -> g\n"));
    const auto id = m.duplicate_node("g");
    CHECK(id == "gain");
    const auto& doc = m.doc();
    REQUIRE(doc.nodes.size() == 3);
    CHECK(doc.nodes[2].id == "gain");
    CHECK(doc.nodes[2].type == "gain");
    CHECK(doc.nodes[2].params.at("db") == "-6");
    CHECK(doc.nodes[2].comments.after == "# quieter");
    CHECK(doc.edges.size() == 1);
    CHECK(m.selection.kind == Selection::Kind::node);
    CHECK(m.selection.node == "gain");
    CHECK(m.duplicate_node("gain") == "gain2");
    CHECK(m.duplicate_node("missing").empty());
    REQUIRE(m.undo());
    CHECK(m.doc().nodes.size() == 3);
}
