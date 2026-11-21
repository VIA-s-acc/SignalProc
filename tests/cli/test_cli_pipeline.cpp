#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <functional>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "commands/watch.hpp"
#include "i18n/i18n.hpp"
#include "theory/cards.hpp"
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
    const auto path = write_sig(dir, R"(pipeline "spec" v2
node src  = wav(path="test.wav")
node ch   = split(mode=lr)
node dwt  = dwt(wavelet=db2, levels=6, ext=symmetric)
node thr  = threshold(rule=bayes, mode=soft, levels=1..4, sigma=mad_d1)
node idwt = idwt(wavelet=db2, ext=symmetric)
node chk  = check(kind=pr, tol=1e-12)
node out  = wav_out(path="out.wav", bits=24, dither=yes)
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
    const auto path = write_sig(dir, R"(pipeline "conv" v2
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
    const auto path = write_sig(dir, R"(pipeline "stereo" v2
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
    const auto path = write_sig(dir, R"(pipeline "report" v2
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

TEST_CASE("sweep runs the graph for every value and writes a table, csv, svg and json", "[cli][pipeline][sweep]") {
    const auto dir = fresh_dir("cli-sweep");
    const auto path = write_sig(dir, R"(node x = samples(values="1 -2 3 -4", rate=8)
node g = gain(db=0)
node s = stats()
x -> g -> s
)");
    const auto csv = dir / "sweep.csv", svg = dir / "sweep.svg";
    const auto text = run({"signals", "sweep", path.string(), "--vary", "g.db=0:12:6", "--measure", "g:peak", "--measure", "s.rms", "--csv", csv.string(), "--svg",
                           svg.string()});
    INFO(text.err);
    REQUIRE(text.code == 0);
    CHECK(text.out.find("g.db") != std::string::npos);
    CHECK(text.out.find("g:peak") != std::string::npos);
    CHECK(text.out.find("7.98") != std::string::npos);
    const auto table = bytes_of(csv);
    CHECK(table.starts_with("g.db,g:peak,s.rms\n0,4,"));
    CHECK(std::count(table.begin(), table.end(), '\n') == 4);
    CHECK(bytes_of(svg).starts_with("<svg"));
    const auto js = run({"signals", "sweep", path.string(), "--vary", "g.db=0,oops", "--measure", "g:peak", "--json"});
    REQUIRE(js.code == 0);
    const auto j = nlohmann::json::parse(js.out);
    CHECK(j["param"] == "g.db");
    REQUIRE(j["rows"].size() == 2);
    CHECK(j["rows"][0]["results"][0] == 4.0);
    CHECK(j["rows"][1]["results"][0].is_null());
    CHECK_FALSE(j["rows"][1]["error"].get<std::string>().empty());
    const auto wrong = run({"signals", "sweep", path.string(), "--vary", "g.nope=0,1", "--measure", "g:peak"});
    CHECK(wrong.code == 1);
    CHECK(wrong.err.find("nope") != std::string::npos);
}


TEST_CASE("explain describes a block, its ports, its parameters with their values and its theory card", "[cli][pipeline][explain]") {
    const auto r = run({"signals", "explain", "gen"});
    CHECK(r.code == 0);
    CHECK(r.out.starts_with("gen"));
    CHECK(r.out.find(signals::tr("pipeline.block.gen")) != std::string::npos);
    CHECK(r.out.find(signals::tr("pipeline.about.gen")) != std::string::npos);
    CHECK(r.out.find("kind = sine") != std::string::npos);
    CHECK(r.out.find("white — " + signals::tr("pipeline.param.gen.kind.white")) != std::string::npos);
    CHECK(r.out.find("channels = 1 (1…64)") != std::string::npos);
    CHECK(r.out.find(signals::tr("pipeline.param.gen.channels")) != std::string::npos);
    const auto* card = signals::theory::find_card("generator");
    REQUIRE(card);
    CHECK(r.out.find(card->title) != std::string::npos);
    const auto bad = run({"signals", "explain", "gian"});
    CHECK(bad.code != 0);
    CHECK(bad.err.find("gain") != std::string::npos);
}

TEST_CASE("run --values N prints the first N samples of every signal probe", "[cli][pipeline][values]") {
    const auto dir = fresh_dir("cli-run-values");
    const auto sig = write_sig(dir, "pipeline \"v\" v2\nnode x = samples(values=\"1 2 3 4 5 6 7 8\", rate=8)\nprobe x as \"x\"\n").string();
    const auto all = run({"signals", "run", sig});
    CHECK(all.out.find("ch0: 1 2 3 4 5 6 7 8") != std::string::npos);
    const auto three = run({"signals", "run", sig, "--values", "3"});
    CHECK(three.code == 0);
    CHECK(three.out.find("ch0: 1 2 3 …") != std::string::npos);
    CHECK(three.out.find("ch0: 1 2 3 4") == std::string::npos);
    const auto none = run({"signals", "run", sig, "--values", "0"});
    CHECK(none.out.find("ch0:") == std::string::npos);
    const auto json = nlohmann::json::parse(run({"signals", "run", sig, "--values", "2", "--json"}).out);
    CHECK(json["probes"]["x"]["values"] == nlohmann::json::array({{1.0, 2.0}}));
}

TEST_CASE("watch reruns when the graph or an input changes and never because of an output", "[cli][pipeline][watch]") {
    namespace fs = std::filesystem;
    const auto dir = fresh_dir("cli-watch");
    const auto sig = write_sig(dir, "pipeline \"w\" v2\nnode src = wav(path=\"in.wav\")\nnode out = wav_out(path=\"out.wav\")\nsrc -> out\n");
    std::ofstream{dir / "in.wav"} << "x";
    std::ofstream{dir / "out.wav"} << "x";
    const auto files = signals::cli::watched_files(sig);
    CHECK(files == std::vector<fs::path>{fs::absolute(sig), fs::absolute(dir / "in.wav")});
    int runs = 0, ticks = 0;
    const auto start = fs::last_write_time(sig);
    const auto code = signals::cli::watch_loop([&] { return ++runs, 0; }, [&] { return signals::cli::watched_files(sig); }, [&] { return ticks < 6; },
                                               [&] {
                                                   ++ticks;
                                                   if (ticks == 2) fs::last_write_time(dir / "out.wav", start + std::chrono::seconds{10});
                                                   if (ticks == 3) fs::last_write_time(dir / "in.wav", start + std::chrono::seconds{20});
                                                   if (ticks == 5) fs::last_write_time(sig, start + std::chrono::seconds{30});
                                               });
    CHECK(code == 0);
    CHECK(runs == 3);
}

TEST_CASE("watch notices an edit that keeps the file time", "[cli][pipeline][watch]") {
    namespace fs = std::filesystem;
    const auto dir = fresh_dir("cli-watch-same-time");
    const auto sig = write_sig(dir, "pipeline \"w\" v2\nnode x = gain(db=-6)\n");
    const auto start = fs::last_write_time(sig);
    int runs = 0, ticks = 0;
    signals::cli::watch_loop([&] { return ++runs, 0; }, [&] { return signals::cli::watched_files(sig); }, [&] { return ticks < 8; }, [&] {
        ++ticks;
        if (ticks == 2) write_sig(dir, "pipeline \"w\" v2\n");
        if (ticks == 5) write_sig(dir, "pipeline \"x\" v2\n");
        fs::last_write_time(sig, start);
    });
    CHECK(runs == 3);
}

TEST_CASE("explain shows the python form", "[cli-pipeline]") {
    const auto r = run({"signals", "explain", "dwt"});
    CHECK(r.code == 0);
    CHECK(r.out.find(signals::tr("cli.explain.python")) != std::string::npos);
    CHECK(r.out.find("pywt.wavedec(") != std::string::npos);
}

TEST_CASE("explain --json has every field", "[cli-pipeline]") {
    const auto all = nlohmann::json::parse(run({"signals", "blocks", "--json"}).out);
    REQUIRE(all.is_array());
    REQUIRE_FALSE(all.empty());
    for (const auto& b : all) {
        const auto id = b.at("id").get<std::string>();
        INFO(id);
        const auto r = run({"signals", "explain", id, "--json"});
        REQUIRE(r.code == 0);
        const auto j = nlohmann::json::parse(r.out);
        for (const char* key : {"id", "title", "about", "inputs", "outputs", "params", "theory", "python"}) CHECK(j.contains(key));
        CHECK(j.at("id") == id);
        CHECK(j.at("params") == b.at("params"));
        CHECK(j.at("inputs") == b.at("inputs"));
        CHECK(j.at("outputs") == b.at("outputs"));
        CHECK_FALSE(j.at("python").get<std::string>().empty());
        CHECK(j.at("python") == b.at("python"));
        if (!j.at("theory").is_null()) {
            CHECK(j.at("theory").contains("kind"));
            CHECK(j.at("theory").contains("id"));
            CHECK(j.at("theory").contains("title"));
        }
    }
}

namespace {

std::string watch_json(std::initializer_list<std::string_view> args, const std::function<void()>& meanwhile) {
    std::thread stopper{[&] {
        meanwhile();
        signals::cli::stop_watching();
    }};
    const auto r = run(args);
    stopper.join();
    return r.out;
}

void wait_for(const std::function<bool()>& done) {
    for (int k = 0; k < 1000 && !done(); ++k) std::this_thread::sleep_for(std::chrono::milliseconds{10});
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);)
        if (!line.empty()) out.push_back(line);
    return out;
}

bool same_json(const nlohmann::json& a, const nlohmann::json& b) {
    if (a.is_number() && b.is_number()) return true;
    if (a.type() != b.type() || a.size() != b.size()) return false;
    if (a.is_object()) {
        for (const auto& [key, value] : b.items())
            if (!a.contains(key) || !same_json(a.at(key), value)) return false;
        return true;
    }
    if (a.is_array()) {
        for (std::size_t k = 0; k < a.size(); ++k)
            if (!same_json(a[k], b[k])) return false;
        return true;
    }
    return a == b;
}

constexpr const char* kWatched = "pipeline \"w\" v2\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode y = gain(db=-6)\nx -> y\nprobe y as \"y\"\n";

}  // namespace

TEST_CASE("watch json prints one document per line on stdout", "[cli-watch]") {
    const auto dir = fresh_dir("cli-watch-json");
    const auto sig = write_sig(dir, kWatched);
    const auto npz = dir / "y.npz";
    const auto out = watch_json({"signals", "run", sig.string(), "--watch", "--json", "--npz", npz.string()}, [&] {
        wait_for([&] { return std::filesystem::exists(npz); });
        const auto first = std::filesystem::last_write_time(npz);
        for (int k = 0; k < 25 && std::filesystem::last_write_time(npz) == first; ++k) {
            write_sig(dir, std::string{kWatched}.replace(std::string{kWatched}.find("-6"), 2, k % 2 ? "-9" : "-12"));
            std::this_thread::sleep_for(std::chrono::milliseconds{400});
        }
    });
    const auto lines = lines_of(out);
    REQUIRE(lines.size() >= 2);
    for (const auto& line : lines) {
        INFO(line);
        const auto j = nlohmann::json::parse(line);
        CHECK(j.contains("nodes"));
        CHECK(j.at("pipeline") == "w");
    }
}

TEST_CASE("watch skips a half-written file", "[cli-watch]") {
    const auto dir = fresh_dir("cli-watch-partial");
    const auto sig = write_sig(dir, kWatched);
    const auto npz = dir / "y.npz";
    const auto out = watch_json({"signals", "run", sig.string(), "--watch", "--json", "--npz", npz.string()}, [&] {
        wait_for([&] { return std::filesystem::exists(npz); });
        { std::ofstream truncated{sig, std::ios::binary | std::ios::trunc}; }
        std::this_thread::sleep_for(std::chrono::milliseconds{900});
        const auto before = std::filesystem::last_write_time(npz);
        write_sig(dir, std::string{kWatched}.replace(std::string{kWatched}.find("-6"), 2, "-12"));
        for (int k = 0; k < 25 && std::filesystem::last_write_time(npz) == before; ++k) std::this_thread::sleep_for(std::chrono::milliseconds{400});
    });
    const auto lines = lines_of(out);
    INFO(out);
    CHECK(lines.size() >= 2);
    for (const auto& line : lines) {
        INFO(line);
        const auto j = nlohmann::json::parse(line);
        CHECK_FALSE(j.contains("error"));
        CHECK(j.value("pipeline", std::string{}) == "w");
        CHECK_FALSE(j.value("nodes", nlohmann::json::array()).empty());
    }
}

TEST_CASE("watch json reports a sig error with its line", "[cli-watch]") {
    const auto dir = fresh_dir("cli-watch-error");
    const auto sig = write_sig(dir, std::string{kWatched} + "node bad = nosuchblock()\n");
    const auto out = watch_json({"signals", "run", sig.string(), "--watch", "--json"}, [] { std::this_thread::sleep_for(std::chrono::milliseconds{800}); });
    const auto lines = lines_of(out);
    REQUIRE(lines.size() == 1);
    const auto j = nlohmann::json::parse(lines[0]);
    REQUIRE(j.contains("error"));
    CHECK(j.at("error").at("line") == 6);
    CHECK(j.at("error").at("file").get<std::string>().ends_with("graph.sig"));
    CHECK_FALSE(j.at("error").at("message").get<std::string>().empty());
}

TEST_CASE("the vscode fixture matches the current output", "[cli-watch]") {
    const auto dir = fresh_dir("cli-watch-fixture");
    const auto npz = dir / "done.npz";
    const auto sig = std::filesystem::path{SIGNALS_SOURCE_DIR} / "examples" / "first_steps.sig";
    const auto out = watch_json({"signals", "run", sig.string(), "--watch", "--json", "--values", "256", "--npz", npz.string()},
                                [&] { wait_for([&] { return std::filesystem::exists(npz); }); });
    const auto lines = lines_of(out);
    REQUIRE(lines.size() == 1);
    auto j = nlohmann::json::parse(lines[0]);
    for (auto& n : j.at("nodes")) n["wall_ms"] = 0, n["cpu_ms"] = 0;
    const auto fixture = std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "vscode" / "first_steps.ndjson";
    if (std::getenv("SIGNALS_UPDATE_SNAPSHOTS")) {
        std::filesystem::create_directories(fixture.parent_path());
        std::ofstream{fixture, std::ios::binary} << j.dump() << '\n';
    }
    CHECK(same_json(j, nlohmann::json::parse(bytes_of(fixture))));
}

TEST_CASE("run json lists the bands of a coefficient probe", "[cli-pipeline]") {
    const auto dir = fresh_dir("cli-json-bands");
    const auto sig = write_sig(dir, "pipeline \"b\" v2\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode c = dwt(wavelet=db2, levels=2)\nx -> c\nprobe c as \"c\"\n");
    const auto r = run({"signals", "run", sig.string(), "--json"});
    REQUIRE(r.code == 0);
    const auto bands = nlohmann::json::parse(r.out).at("probes").at("c").at("bands");
    REQUIRE(bands.size() == 3);
    CHECK(bands[0].at("name") == "a2");
    CHECK(bands[2].at("name") == "d1");
    std::size_t total = 0;
    for (const auto& b : bands) {
        CHECK(b.at("energy").get<double>() >= 0);
        total += b.at("length").get<std::size_t>();
    }
    CHECK(total == nlohmann::json::parse(r.out).at("probes").at("c").at("length"));
}

TEST_CASE("seeded noise and dither give the pinned samples of the Windows CI step", "[cli-pipeline]") {
    const auto sig = std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "noise" / "pinned.sig";
    const auto r = run({"signals", "run", sig.string(), "--json", "--values", "4"});
    REQUIRE(r.code == 0);
    const auto probes = nlohmann::json::parse(r.out).at("probes");
    CHECK(probes.at("w").at("values") == nlohmann::json::parse("[[-0.04520348258180983, -0.4438112180588447, -0.2856173093236448, 0.787991230653915]]"));
    CHECK(probes.at("d").at("values") == nlohmann::json::parse("[[-0.046875, -0.4375, -0.2890625, 0.7890625]]"));
}
