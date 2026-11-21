#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <string>

#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/flatten.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/npz_values.hpp"
#include "pipeline/sig.hpp"
#include "pipeline/sweep.hpp"

using namespace signals;
using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;

namespace {

PipelineDoc doc_of(std::string_view text) {
    auto loaded = load_sig(text);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    return loaded->doc;
}

std::vector<std::pair<std::string, std::vector<double>>> probes(const PipelineDoc& doc) {
    const auto graph = build_graph(doc, std::filesystem::temp_directory_path());
    INFO((graph ? std::string{} : graph.error().message));
    REQUIRE(graph);
    ResultCache cache{0};
    core::CancelToken cancel;
    const auto run = run_graph(*graph, cache, {}, cancel);
    REQUIRE(run);
    std::vector<std::pair<std::string, std::vector<double>>> out;
    for (const auto& [name, value] : run->probes) {
        auto entries = value_entries(value);
        REQUIRE(entries);
        for (const auto& [key, array] : *entries)
            if (const auto* numbers = std::get_if<std::vector<double>>(&array.data)) out.emplace_back(name + "." + key, *numbers);
    }
    return out;
}

std::string build_error(std::string_view text) {
    const auto graph = build_graph(doc_of(text), std::filesystem::temp_directory_path());
    REQUIRE_FALSE(graph);
    return graph.error().message;
}

}  // namespace

TEST_CASE("a variable feeds params of every kind it may", "[vars]") {
    const auto with = doc_of("pipeline \"v\" v2\n"
                             "let fs = 8000\nlet lv = 3\nlet w = db4\n"
                             "node x = gen(kind=sine, freq=440, rate=$fs, duration=0.05)\n"
                             "node d = dwt(wavelet=$w, levels=$lv)\n"
                             "node lp = biquad(type=lowpass, f0=\"$fs/8\")\n"
                             "x -> d\nx -> lp\nprobe d.a as \"a\"\nprobe lp as \"lp\"\n");
    const auto plain = doc_of("pipeline \"v\" v2\n"
                              "node x = gen(kind=sine, freq=440, rate=8000, duration=0.05)\n"
                              "node d = dwt(wavelet=db4, levels=3)\n"
                              "node lp = biquad(type=lowpass, f0=1000)\n"
                              "x -> d\nx -> lp\nprobe d.a as \"a\"\nprobe lp as \"lp\"\n");
    CHECK(probes(with) == probes(plain));
}

TEST_CASE("an expression variable is parenthesized", "[vars]") {
    const auto flat = flatten(doc_of("pipeline \"v\" v2\nlet fs = \"2*4000\"\nnode lp = biquad(type=lowpass, f0=\"$fs/4\")\n"));
    REQUIRE(flat);
    CHECK(flat->nodes.at(0).params.at("f0") == "(2*4000)/4");
    CHECK(flat->nodes.at(0).source.at("f0") == "$fs/4");
    const auto graph = build_graph(doc_of("pipeline \"v\" v2\nlet fs = \"2*4000\"\nnode lp = biquad(type=lowpass, f0=\"$fs/4\")\n"), ".", {.partial = true});
    REQUIRE(graph);
    CHECK(param_real(graph->nodes.at(0).params, "f0") == 2000);
}

TEST_CASE("variables may reference each other in any order", "[vars]") {
    const auto flat = flatten(doc_of("pipeline \"v\" v2\nlet half = \"$fs/2\"\nlet fs = 8000\nnode g = gain(db=\"$half/4000\")\n"));
    REQUIRE(flat);
    CHECK(flat->nodes.at(0).params.at("db") == "(8000/2)/4000");
}

TEST_CASE("a cycle names every variable in it", "[vars]") {
    const auto message = build_error("pipeline \"v\" v2\nlet a = \"$b+1\"\nlet b = \"$c+1\"\nlet c = \"$a+1\"\nnode g = gain(db=$a)\n");
    CHECK_THAT(message, ContainsSubstring("a") && ContainsSubstring("b") && ContainsSubstring("c") && ContainsSubstring("→"));
}

TEST_CASE("an unknown variable suggests the nearest", "[vars]") {
    const auto message = build_error("pipeline \"v\" v2\nlet level = 3\nnode d = dwt(levels=$levl)\n");
    CHECK_THAT(message, ContainsSubstring("levl") && ContainsSubstring("level"));
}

TEST_CASE("an expression is refused where only whole values are allowed", "[vars]") {
    const auto message = build_error("pipeline \"v\" v2\nlet t = 0.1s\nnode c = cut(begin=\"$t*2\")\n");
    CHECK_THAT(message, ContainsSubstring("begin"));
    const auto whole = build_graph(doc_of("pipeline \"v\" v2\nlet t = 0.1s\nnode c = cut(begin=$t)\n"), ".", {.partial = true});
    CHECK(whole);
}

TEST_CASE("dollars are literal when doubled and text params take variables", "[vars]") {
    const auto flat = flatten(doc_of("pipeline \"v\" v2\nlet dir = sounds\nnode x = wav(path=\"$dir/a$$b.wav\")\n"));
    REQUIRE(flat);
    CHECK(flat->nodes.at(0).params.at("path") == "sounds/a$b.wav");
}

TEST_CASE("set and sweep change a variable", "[vars]") {
    auto doc = doc_of("pipeline \"v\" v2\nlet lv = 2\nnode x = gen(kind=sine, rate=8000, duration=0.05)\nnode d = dwt(levels=$lv)\nnode s = stats()\nx -> d\nd.a -> s\n");
    REQUIRE(apply_set(doc, "lv=5"));
    CHECK(doc.lets.at(0).value == "5");
    CHECK_FALSE(apply_set(doc, "lvl=5"));
    const auto axis = parse_vary("lv=1:6@6");
    REQUIRE(axis);
    core::CancelToken cancel;
    const auto r = run_sweep(doc, ".", *axis, {*parse_measure("s.rms_ch0")}, cancel, {});
    REQUIRE(r);
    CHECK(r->rows.size() == 6);
    for (const auto& row : r->rows) CHECK(row.error.empty());
}

TEST_CASE("let lines round-trip with comments", "[vars]") {
    const std::string text = "pipeline \"v\" v2\n\n# sample rate\nlet fs = 8000  # Hz\nlet half = \"$fs/2\"\n\nnode g = gain(db=$fs)\n";
    const auto doc = doc_of(text);
    REQUIRE(doc.lets.size() == 2);
    CHECK(write_sig(doc) == write_sig(doc_of(write_sig(doc))));
    CHECK_THAT(write_sig(doc), ContainsSubstring("# sample rate\nlet fs = 8000  # Hz\nlet half = \"$fs/2\""));
    CHECK_THAT(write_sig(doc), ContainsSubstring("db=$fs"));
}

TEST_CASE("a variable declared twice is refused with both lines", "[vars]") {
    const auto doc = load_sig("pipeline \"v\" v2\nlet a = 1\nlet a = 2\n");
    REQUIRE_FALSE(doc);
    CHECK(doc.error().line == 3);
}

TEST_CASE("a variable that doubles itself on every level is refused quickly", "[vars]") {
    std::string text = "pipeline \"v\" v2\nlet v0 = 1\n";
    for (int k = 1; k <= 40; ++k) text += "let v" + std::to_string(k) + " = \"$v" + std::to_string(k - 1) + "+$v" + std::to_string(k - 1) + "\"\n";
    text += "node g = gain(db=$v40)\n";
    const auto start = std::chrono::steady_clock::now();
    const auto graph = build_graph(doc_of(text), ".", {.partial = true});
    CHECK_FALSE(graph);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{2});
}

TEST_CASE("variables nested deeper than the limit are refused", "[vars]") {
    std::string text = "pipeline \"v\" v2\nlet v0 = 1\n";
    for (int k = 1; k <= 100; ++k) text += "let v" + std::to_string(k) + " = $v" + std::to_string(k - 1) + "\n";
    text += "node g = gain(db=$v100)\n";
    CHECK_FALSE(build_graph(doc_of(text), ".", {.partial = true}));
}

TEST_CASE("a long variable substituted many times is refused for the whole graph", "[vars]") {
    std::string text = "pipeline \"v\" v2\nlet big = \"" + std::string(40000, 'a') + "\"\n";
    for (int k = 0; k < 300; ++k) text += "node n" + std::to_string(k) + " = wav(path=$big)\n";
    const auto flat = flatten(doc_of(text));
    REQUIRE_FALSE(flat);
    CHECK_THAT(flat.error().message, ContainsSubstring("8388608"));
}

TEST_CASE("a variable shared by many nodes and variables keeps its value", "[vars]") {
    std::string text = "pipeline \"v\" v2\nlet c0 = 1\n";
    for (int k = 1; k <= 12; ++k) text += "let c" + std::to_string(k) + " = \"$c" + std::to_string(k - 1) + "+$c" + std::to_string(k - 1) + "\"\n";
    for (int k = 0; k < 100; ++k) text += "let d" + std::to_string(k) + " = $c12\n";
    for (int k = 0; k < 100; ++k) text += "node g" + std::to_string(k) + " = gain(db=$d" + std::to_string(k) + ")\n";
    const auto start = std::chrono::steady_clock::now();
    const auto flat = flatten(doc_of(text));
    INFO((flat ? std::string{} : flat.error().message));
    REQUIRE(flat);
    CHECK(flat->nodes.at(99).params.at("db").size() > 15000);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds{50});
}

TEST_CASE("a deep chain is refused even when its tail was expanded before", "[vars]") {
    std::string text = "pipeline \"v\" v2\nlet v0 = 1\n";
    for (int k = 1; k <= 100; ++k) text += "let v" + std::to_string(k) + " = $v" + std::to_string(k - 1) + "\n";
    text += "node a = gain(db=$v50)\nnode b = gain(db=$v100)\n";
    const auto flat = flatten(doc_of(text));
    REQUIRE_FALSE(flat);
    CHECK(flat.error().line == 66);
}

TEST_CASE("set on a node checks its other params with variables substituted", "[vars]") {
    auto doc = doc_of("pipeline \"v\" v2\nlet r = 8\nnode x = samples(values=\"1 2 3 4\", rate=$r)\n");
    const auto ok = apply_set(doc, "x.values=\"4 3 2 1\"");
    INFO((ok ? std::string{} : ok.error().message));
    CHECK(ok);
    CHECK_FALSE(apply_set(doc, "x.rate=fast"));
}
