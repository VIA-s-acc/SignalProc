#include <catch2/catch_test_macros.hpp>

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
