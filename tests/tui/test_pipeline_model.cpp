#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "commands/cli.hpp"
#include "pipeline/sig.hpp"
#include "support/paths.hpp"
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
