#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "support/paths.hpp"
#include "tui/widgets/text_util.hpp"

namespace {

struct Ran {
    int code;
    std::string out, err;
};

Ran run(std::initializer_list<std::string_view> args) {
    std::ostringstream out, err;
    std::vector<std::string_view> v(args);
    const int code = signals::cli::run_cli(v, out, err);
    return {code, out.str(), err.str()};
}

std::string bytes_of(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path fresh_dir(const std::string& name) {
    const auto dir = signals::test::temp_path(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path write_sig(const std::filesystem::path& dir, const std::string& text) {
    const auto path = dir / "graph.sig";
    std::ofstream{path} << text;
    return path;
}

const std::string kDenoise = std::string{SIGNALS_SOURCE_DIR} + "/examples/denoise.sig";

}  // namespace

TEST_CASE("run denoise.sig writes the same samples as signals denoise", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-run-denoise");
    const auto out = (dir / "out.wav").string(), ref = (dir / "ref.wav").string();
    const auto sample = (std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav").string();
    const auto r = run({"signals", "run", kDenoise, "-o", out});
    INFO(r.err);
    REQUIRE(r.code == 0);
    REQUIRE(run({"signals", "denoise", sample, "-o", ref, "--wavelet", "sym8", "--rule", "bayes", "--mode", "soft", "--dither", "none"}).code == 0);
    CHECK(bytes_of(out) == bytes_of(ref));
    CHECK(r.out.find("idwt") != std::string::npos);
    CHECK(r.out.find("d1") != std::string::npos);
}

TEST_CASE("run --json lists nodes with time and state and the probes", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-run-json");
    const auto r = run({"signals", "run", kDenoise, "-o", (dir / "out.wav").string(), "--json"});
    INFO(r.err);
    REQUIRE(r.code == 0);
    const auto j = nlohmann::json::parse(r.out);
    REQUIRE(j["nodes"].size() == 6);
    for (const auto& n : j["nodes"]) {
        CHECK(n["state"] == "done");
        CHECK(n["wall_ms"].get<double>() >= 0);
    }
    CHECK(j["nodes"][0]["id"] == "src");
    REQUIRE(j["probes"].contains("d1"));
    CHECK(j["probes"]["d1"]["kind"] == "signal");
    CHECK(j["probes"]["d1"]["length"].get<std::size_t>() > 0);
    CHECK(j["checks_passed"] == true);
}

TEST_CASE("run --set and --probe-dir reach the graph and the files", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-run-set");
    const auto r = run({"signals", "run", kDenoise, "-o", (dir / "out.wav").string(), "--set", "thr.mode=hard", "--probe-dir",
                        (dir / "probes").string(), "--json"});
    INFO(r.err);
    REQUIRE(r.code == 0);
    CHECK(std::filesystem::exists(dir / "probes" / "d1.wav"));
    const auto bad = run({"signals", "run", kDenoise, "--set", "thr.mod=hard"});
    CHECK(bad.code == 1);
    CHECK(bad.err.find("mode") != std::string::npos);
}

TEST_CASE("a broken graph exits with 1 and names the .sig line", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-run-broken");
    const auto path = write_sig(dir, "node a = gen()\nnode b = gen()\na -> b\n");
    const auto r = run({"signals", "run", path.string()});
    CHECK(r.code == 1);
    CHECK(r.err.find("graph.sig:3") != std::string::npos);
    const auto parse = write_sig(dir, "node a = gen(\n");
    const auto p = run({"signals", "run", parse.string()});
    CHECK(p.code == 1);
    CHECK(p.err.find("graph.sig:1") != std::string::npos);
    CHECK(run({"signals", "run", (dir / "missing.sig").string()}).code == 1);
}

TEST_CASE("a failed check exits with 3", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-run-check");
    const auto path = write_sig(dir, R"(node src = gen(duration=0.1)
node g = gain(db=-6)
node chk = check(kind=pr, tol=1e-9)
src -> g -> chk.in
src -> chk.ref
)");
    const auto r = run({"signals", "run", path.string()});
    CHECK(r.code == 3);
}

TEST_CASE("graph draws every node within 120 columns and ASCII stays ASCII", "[cli][pipeline]") {
    const auto dir = fresh_dir("cli-graph");
    const auto path = write_sig(dir, R"(pipeline "spec" v1
node src  = wav(path="test.wav")
node ch   = split(mode=lr)
node dwt  = dwt(wavelet=db2, levels=6, ext=symmetric)
node thr  = threshold(rule=bayes, mode=soft, levels=1..4, sigma=mad_d1)
node idwt = idwt(wavelet=db2, ext=symmetric)
node chk  = check(kind=pr, tol=1e-12)
node out  = wav_out(path="out.wav", bits=24, dither=tpdf)
src -> ch
ch.l -> dwt -> thr -> idwt -> out
src -> chk.ref
idwt -> chk.in
probe dwt.d3 as "band_d3"
)");
    for (const bool ascii : {false, true}) {
        const auto r = ascii ? run({"signals", "graph", path.string(), "--ascii"}) : run({"signals", "graph", path.string()});
        INFO(r.out << r.err);
        REQUIRE(r.code == 0);
        for (const char* id : {"src", "ch", "dwt", "thr", "idwt", "chk", "out"}) CHECK(r.out.find(std::string{"["} + id) != std::string::npos);
        std::istringstream lines{r.out};
        for (std::string line; std::getline(lines, line);) CHECK(signals::tui::display_width(line) <= 120);
        if (ascii)
            for (unsigned char c : r.out) CHECK(c < 0x80);
        else
            CHECK(r.out.find("▼") != std::string::npos);
    }
}

TEST_CASE("paths with spaces and Cyrillic work from the .sig to the output and probes", "[cli][pipeline][review]") {
    const auto dir = fresh_dir("cli путь с пробелами");
    std::filesystem::copy_file(std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav", dir / "звук 1.wav");
    const auto path = dir / "граф шум.sig";
    std::ofstream{path} << "node src = wav(path=\"звук 1.wav\")\nnode g = gain(db=-6)\nnode out = wav_out(path=\"тихо.wav\")\nsrc -> g -> out\nprobe g as \"щуп\"\n";
    const auto r = run({"signals", "run", path.string(), "--probe-dir", (dir / "щупы").string()});
    INFO(r.err);
    REQUIRE(r.code == 0);
    CHECK(std::filesystem::exists(dir / "тихо.wav"));
    CHECK(std::filesystem::exists(dir / "щупы" / "щуп.wav"));
}

TEST_CASE("run prints the samples of a short probe and puts them into the JSON", "[cli][pipeline][values]") {
    const auto dir = fresh_dir("cli-run-values");
    const auto path = write_sig(dir, R"(pipeline "conv" v1
node x = samples(values="2 -1 3 1", rate=1)
node xp = pad(after=1)
node y1 = fir_taps(taps="0.5 -0.5")
node tone = gen(duration=0.1)
x -> xp -> y1
probe y1 as "y1"
probe tone as "tone"
)");
    const auto text = run({"signals", "run", path.string()});
    INFO(text.err);
    REQUIRE(text.code == 0);
    CHECK(text.out.find("ch0: 1 -1.5 2 -1 -0.5\n") != std::string::npos);
    CHECK(text.out.find("ch0: 0 ") == std::string::npos);
    const auto r = run({"signals", "run", path.string(), "--json"});
    REQUIRE(r.code == 0);
    const auto j = nlohmann::json::parse(r.out);
    CHECK(j["probes"]["y1"]["values"] == nlohmann::json::parse("[[1, -1.5, 2, -1, -0.5]]"));
    CHECK_FALSE(j["probes"]["tone"].contains("values"));
}

TEST_CASE("the prak1 example graphs give the numbers of the notebook", "[cli][pipeline][values][examples]") {
    const auto dir = std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "prak1";
    const auto first = run({"signals", "run", (dir / "zadanie1.sig").string(), "--json"});
    INFO(first.err);
    REQUIRE(first.code == 0);
    const auto j1 = nlohmann::json::parse(first.out);
    CHECK(j1["probes"]["y0 = x*h0"]["values"] == nlohmann::json::parse("[[1, 0.5, 1, 2, 0.5]]"));
    CHECK(j1["probes"]["y1 = x*h1"]["values"] == nlohmann::json::parse("[[1, -1.5, 2, -1, -0.5]]"));
    const auto fifth = run({"signals", "run", (dir / "zadanie5.sig").string(), "--json"});
    INFO(fifth.err);
    REQUIRE(fifth.code == 0);
    const auto j5 = nlohmann::json::parse(fifth.out);
    const auto field = [&](const char* probe, const char* name) { return j5["probes"][probe]["fields"][name].get<double>(); };
    CHECK(j5["probes"]["вход x"]["length"] == 128);
    CHECK(std::abs(field("пики x", "peak1_hz") - 78.125) < 1e-9);
    CHECK(std::abs(field("пики x", "peak2_hz") - 398.4375) < 1e-9);
    const double h0_low = field("пики y0", "peak1_db") - field("пики x", "peak1_db");
    const double h0_high = field("пики y0", "peak2_db") - field("пики x", "peak2_db");
    const double h1_high = field("пики y1", "peak1_db") - field("пики x", "peak2_db");
    const double h1_low = field("пики y1", "peak2_db") - field("пики x", "peak1_db");
    CHECK(std::abs(std::pow(10.0, h0_low / 20) - 0.972) < 0.005);
    CHECK(std::abs(std::pow(10.0, h0_high / 20) - 0.309) < 0.005);
    CHECK(std::abs(std::pow(10.0, h1_high / 20) - 0.951) < 0.005);
    CHECK(std::abs(std::pow(10.0, h1_low / 20) - 0.233) < 0.005);
}

TEST_CASE("run shows the shape of every node and of a stereo probe", "[cli][pipeline][multichannel]") {
    const auto dir = fresh_dir("cli-run-shape");
    const auto path = write_sig(dir, R"(pipeline "stereo" v1
node x = samples(values="1 2 3 | 4 5 6", rate=8)
node g = gain(db=0)
x -> g
probe g as "g"
)");
    const auto text = run({"signals", "run", path.string()});
    INFO(text.err);
    REQUIRE(text.code == 0);
    CHECK(text.out.find("2×3") != std::string::npos);
    CHECK(text.out.find("ch1: 4 5 6\n") != std::string::npos);
    const auto r = run({"signals", "run", path.string(), "--json"});
    REQUIRE(r.code == 0);
    const auto j = nlohmann::json::parse(r.out);
    CHECK(j["nodes"][0]["shape"][0]["kind"] == "signal");
    CHECK(j["nodes"][0]["shape"][0]["channels"] == 2);
    CHECK(j["nodes"][0]["shape"][0]["frames"] == 3);
    CHECK(j["probes"]["g"]["values"] == nlohmann::json::parse("[[1, 2, 3], [4, 5, 6]]"));
}

TEST_CASE("run --report writes a markdown report with plots next to it", "[cli][pipeline][export]") {
    const auto dir = fresh_dir("cli-run-report");
    const auto path = write_sig(dir, R"(pipeline "report" v1
node x = samples(values="1 2 3 | 4 5 6", rate=8)
node g = gain(db=0)
node tone = gen(duration=0.1)
node st = stats()
x -> g
tone -> st
probe g as "g"
probe tone as "tone"
probe st as "st"
)");
    const auto report = dir / "out" / "report.md";
    const auto r = run({"signals", "run", path.string(), "--report", report.string()});
    INFO(r.err);
    REQUIRE(r.code == 0);
    REQUIRE(std::filesystem::exists(report));
    const auto md = bytes_of(report);
    CHECK(md.starts_with("# "));
    CHECK(md.find("```") != std::string::npos);
    CHECK(md.find("| g | gain |") != std::string::npos);
    CHECK(md.find("| n | ch0 | ch1 |") != std::string::npos);
    CHECK(md.find("| 2 | 3 | 6 |") != std::string::npos);
    CHECK(md.find("rms") != std::string::npos);
    std::size_t images = 0;
    for (auto at = md.find("]("); at != std::string::npos; at = md.find("](", at + 1)) {
        const auto end = md.find(')', at);
        const auto target = md.substr(at + 2, end - at - 2);
        if (!target.ends_with(".svg")) continue;
        INFO(target);
        CHECK(std::filesystem::exists(report.parent_path() / target));
        CHECK(std::filesystem::exists((report.parent_path() / target).replace_extension(".csv")));
        ++images;
    }
    CHECK(images >= 3);
}

