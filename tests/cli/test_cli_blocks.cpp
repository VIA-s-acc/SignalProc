#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "commands/watch.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/probe_files.hpp"
#include "pipeline/user_blocks.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {

constexpr const char* kGraph = "pipeline \"b\" v2\n"
                               "use \"lib.sig\"\n"
                               "block level(wavelet=haar) {\n"
                               "    in x\n"
                               "    out y, a\n"
                               "    node h0  = convolve(filter=$wavelet.h0, method=direct)\n"
                               "    node da  = downsample(m=2, anti_alias=no, phase=1)\n"
                               "    node ua  = upsample(m=2, anti_image=no)\n"
                               "    node f0  = convolve(filter=$wavelet.f0, method=direct)\n"
                               "    x -> h0 -> da -> ua -> f0 -> y\n"
                               "    da -> a\n"
                               "    probe f0 as \"inside\"\n"
                               "}\n"
                               "node x   = samples(values=\"4 6 10 12 8 6 5 5\", rate=8)\n"
                               "node lv1 = level()\n"
                               "node lv2 = loud()\n"
                               "x -> lv1.x\n"
                               "lv1.a -> lv2\n"
                               "probe lv1.y as \"y: first\"\n"
                               "probe lv2 as \"y/second\"\n";

constexpr const char* kLib = "pipeline \"lib\" v2\nblock loud(db=6) {\n    in x\n    out y\n    node g = gain(db=$db)\n    x -> g -> y\n}\n";

fs::path write_graph(const std::string& name) {
    const auto dir = signals::test::temp_path("cli-blocks") / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream{dir / "lib.sig"} << kLib;
    std::ofstream{dir / "g.sig"} << kGraph;
    return dir / "g.sig";
}

std::tuple<int, std::string, std::string> run(std::vector<std::string> args) {
    std::ostringstream out, err;
    std::vector<std::string_view> views{args.begin(), args.end()};
    const int code = signals::cli::run_cli(views, out, err);
    return {code, out.str(), err.str()};
}

}  // namespace

TEST_CASE("set changes a call param and refuses an inner node", "[cli-blocks]") {
    const auto sig = write_graph("set");
    const auto [ok, out, err] = run({"signals", "run", sig.string(), "--set", "lv1.wavelet=db2", "--json"});
    INFO(err);
    REQUIRE(ok == 0);
    const auto [bad_param, o2, e2] = run({"signals", "run", sig.string(), "--set", "lv1.wavelets=db2"});
    CHECK(bad_param != 0);
    CHECK_THAT(e2, ContainsSubstring("wavelet"));
    const auto [inner, o3, e3] = run({"signals", "run", sig.string(), "--set", "lv1/h0.method=fft"});
    CHECK(inner != 0);
    auto expected = signals::tr("pipeline.blocks.inner_set");
    for (const auto& [key, value] : {std::pair{"{node}", "lv1/h0"}, std::pair{"{call}", "lv1"}})
        for (auto at = expected.find(key); at != std::string::npos; at = expected.find(key)) expected.replace(at, std::string_view{key}.size(), value);
    CHECK_THAT(e3, ContainsSubstring(expected));
}

TEST_CASE("graph shows calls and expand shows their nodes", "[cli-blocks]") {
    const auto sig = write_graph("graph");
    const auto [code, out, err] = run({"signals", "graph", sig.string()});
    INFO(err);
    REQUIRE(code == 0);
    CHECK_THAT(out, ContainsSubstring("lv1"));
    CHECK_THAT(out, ContainsSubstring("level"));
    CHECK(out.find("lv1/h0") == std::string::npos);
    const auto [code2, expanded, err2] = run({"signals", "graph", sig.string(), "--expand"});
    INFO(err2);
    REQUIRE(code2 == 0);
    CHECK_THAT(expanded, ContainsSubstring("lv1/h0"));
}

TEST_CASE("run groups the nodes of a call under it", "[cli-blocks]") {
    const auto sig = write_graph("run");
    const auto [code, out, err] = run({"signals", "run", sig.string()});
    INFO(err);
    REQUIRE(code == 0);
    const auto call = out.find("lv1");
    const auto inner = out.find("  h0");
    REQUIRE(call != std::string::npos);
    REQUIRE(inner != std::string::npos);
    CHECK(call < inner);
    CHECK_THAT(out, ContainsSubstring("level"));
}

TEST_CASE("json nodes name their call", "[cli-blocks]") {
    const auto sig = write_graph("json");
    const auto [code, out, err] = run({"signals", "run", sig.string(), "--json"});
    INFO(err);
    REQUIRE(code == 0);
    const auto j = nlohmann::json::parse(out);
    bool seen = false;
    for (const auto& n : j.at("nodes"))
        if (n.at("id") == "lv1/h0") seen = n.at("instance") == "lv1";
    CHECK(seen);
}

TEST_CASE("probe names with slash and colon give safe file names", "[cli-blocks]") {
    const std::vector<std::string> names{"y: first", "y/second", "lv1/inside", "y_second", "x̂", "h0 второго уровня"};
    const auto files = signals::pipeline::probe_file_names(names);
    CHECK(files.at("y: first") == "y_ first");
    CHECK(files.at("y/second") == "y_second");
    CHECK(files.at("y_second") == "y_second-2");
    CHECK(files.at("x̂") == "x̂");
    CHECK(files.at("h0 второго уровня") == "h0 второго уровня");
    const auto sig = write_graph("files");
    const auto dir = sig.parent_path() / "probes";
    const auto report = sig.parent_path() / "report.md";
    const auto [code, out, err] = run({"signals", "run", sig.string(), "--probe-dir", dir.string(), "--npz", (sig.parent_path() / "all.npz").string(), "--report", report.string()});
    INFO(err);
    REQUIRE(code == 0);
    std::set<std::string> written;
    for (const auto& e : fs::directory_iterator{dir}) {
        CHECK(e.is_regular_file());
        written.insert(e.path().stem().string());
    }
    CHECK(written.contains("y_ first"));
    CHECK(written.contains("y_second"));
    CHECK(written.contains("lv1_inside"));
    std::size_t plots = 0;
    for (const auto& e : fs::directory_iterator{sig.parent_path() / "report_files"}) plots += e.path().extension() == ".svg";
    CHECK(plots >= 3);
}

TEST_CASE("explain finds a block in the graph file", "[cli-blocks]") {
    const auto sig = write_graph("explain");
    const auto [code, out, err] = run({"signals", "explain", "level", "--graph", sig.string()});
    INFO(err);
    REQUIRE(code == 0);
    CHECK_THAT(out, ContainsSubstring("wavelet"));
    CHECK_THAT(out, ContainsSubstring("haar"));
    CHECK_THAT(out, ContainsSubstring("signal"));
    const auto [code2, out2, err2] = run({"signals", "explain", "loud", "--graph", sig.string()});
    INFO(err2);
    CHECK(code2 == 0);
    CHECK_THAT(out2, ContainsSubstring("db"));
}

TEST_CASE("watch follows the files a graph uses", "[cli-blocks]") {
    const auto sig = write_graph("watch");
    const auto files = signals::cli::watched_files(sig);
    CHECK(std::ranges::find(files, fs::absolute(sig.parent_path() / "lib.sig").lexically_normal()) != files.end());
}

TEST_CASE("explain finds a block in the user library", "[cli-blocks]") {
    const auto home = signals::test::temp_path("cli-blocks") / "config";
    fs::remove_all(home);
    fs::create_directories(home / "signals" / "blocks");
    std::ofstream{home / "signals" / "blocks" / "banks.sig"} << "pipeline \"banks\" v2\n# Halves the level\nblock bank_level(gain=-6) {\n    in x\n    out y\n    node g = gain(db=$gain)\n    x -> g -> y\n}\n";
    const auto env = [&](std::string_view key) -> std::optional<std::string> {
        if (key == "XDG_CONFIG_HOME") return home.string();
        return std::nullopt;
    };
    const auto found = signals::pipeline::find_user_block("bank_level", env);
    REQUIRE(found);
    CHECK(found->file.filename() == "banks.sig");
    REQUIRE(signals::pipeline::find_own_block(found->doc, "bank_level"));
    CHECK_FALSE(signals::pipeline::find_user_block("bank", env));
}
