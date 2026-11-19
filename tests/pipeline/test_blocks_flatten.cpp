#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "core/progress.hpp"
#include "i18n/i18n.hpp"
#include "io/generators.hpp"
#include "io/wav.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/examples.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/flatten.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/npz_values.hpp"
#include "support/paths.hpp"

using namespace signals;
using namespace signals::pipeline;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {

constexpr const char* kLevel = "block bank_level(wavelet=haar) {\n"
                               "    in x\n"
                               "    out y, a, d\n"
                               "    node h0  = convolve(filter=$wavelet.h0, method=direct)\n"
                               "    node h1  = convolve(filter=$wavelet.h1, method=direct)\n"
                               "    node da  = downsample(m=2, anti_alias=no, phase=1)\n"
                               "    node dd  = downsample(m=2, anti_alias=no, phase=1)\n"
                               "    node ua  = upsample(m=2, anti_image=no)\n"
                               "    node ud  = upsample(m=2, anti_image=no)\n"
                               "    node f0  = convolve(filter=$wavelet.f0, method=direct)\n"
                               "    node f1  = convolve(filter=$wavelet.f1, method=direct)\n"
                               "    node sum = add()\n"
                               "    x -> h0 -> da -> ua -> f0 -> sum.a\n"
                               "    x -> h1 -> dd -> ud -> f1 -> sum.b\n"
                               "    da -> a\n"
                               "    dd -> d\n"
                               "    sum -> y\n"
                               "    probe sum as \"xh\"\n"
                               "}\n";

constexpr const char* kSource = "node x = samples(values=\"4 6 10 12 8 6 5 5 | 1 2 3 4 5 6 7 8\", rate=8)\n";

std::string unfolded_level(const std::string& id, const std::string& w) {
    return "node " + id + "_h0 = convolve(filter=" + w + ".h0, method=direct)\nnode " + id + "_h1 = convolve(filter=" + w + ".h1, method=direct)\nnode " + id +
           "_da = downsample(m=2, anti_alias=no, phase=1)\nnode " + id + "_dd = downsample(m=2, anti_alias=no, phase=1)\nnode " + id +
           "_ua = upsample(m=2, anti_image=no)\nnode " + id + "_ud = upsample(m=2, anti_image=no)\nnode " + id + "_f0 = convolve(filter=" + w +
           ".f0, method=direct)\nnode " + id + "_f1 = convolve(filter=" + w + ".f1, method=direct)\nnode " + id + "_sum = add()\n" + id + "_h0 -> " + id + "_da -> " + id +
           "_ua -> " + id + "_f0 -> " + id + "_sum.a\n" + id + "_h1 -> " + id + "_dd -> " + id + "_ud -> " + id + "_f1 -> " + id + "_sum.b\n";
}

std::map<std::string, io::NpzEntries> probes_of(const std::string& text, const fs::path& dir = fs::temp_directory_path()) {
    const auto loaded = load_sig(text, {dir});
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded);
    const auto graph = build_graph(loaded->doc, dir);
    INFO((graph ? std::string{} : graph.error().message));
    REQUIRE(graph);
    ResultCache cache{0};
    core::CancelToken cancel;
    const auto run = run_graph(*graph, cache, {}, cancel);
    REQUIRE(run);
    std::map<std::string, io::NpzEntries> out;
    for (const auto& [name, value] : run->probes) out[name] = *value_entries(value);
    return out;
}

bool same(const io::NpzEntries& a, const io::NpzEntries& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (a[k].first != b[k].first || a[k].second.shape != b[k].second.shape || !(a[k].second.data == b[k].second.data)) return false;
    return true;
}

std::expected<Graph, SigError> graph_of(const std::string& text, const fs::path& dir = fs::temp_directory_path()) {
    const auto loaded = load_sig(text, {dir});
    if (!loaded) return std::unexpected{loaded.error()};
    return build_graph(loaded->doc, dir);
}

}  // namespace

TEST_CASE("a two-level bank as blocks equals the unfolded graph", "[blocks-flatten]") {
    const auto blocks = probes_of(std::string{"pipeline \"b\" v2\n"} + kLevel + kSource +
                                  "node lv1 = bank_level()\nnode lv2 = bank_level(wavelet=db2)\nx -> lv1.x\nlv1.a -> lv2.x\n"
                                  "probe lv1.d as \"d1\"\nprobe lv2.y as \"y2\"\nprobe lv2/h0 as \"h0b\"\n");
    const auto hand = probes_of(std::string{"pipeline \"h\" v2\n"} + kSource + unfolded_level("lv1", "haar") + unfolded_level("lv2", "db2") +
                                "x -> lv1_h0\nx -> lv1_h1\nlv1_da -> lv2_h0\nlv1_da -> lv2_h1\n"
                                "probe lv1_dd as \"d1\"\nprobe lv2_sum as \"y2\"\nprobe lv2_h0 as \"h0b\"\nprobe lv1_sum as \"lv1/xh\"\nprobe lv2_sum as \"lv2/xh\"\n");
    REQUIRE(blocks.size() == hand.size());
    for (const auto& [name, entries] : hand) {
        INFO(name);
        REQUIRE(blocks.contains(name));
        CHECK(same(blocks.at(name), entries));
    }
}

TEST_CASE("coefficients pass through block ports and blocks nest", "[blocks-flatten]") {
    const std::string defs = std::string{kLevel} +
                             "block analyze(w=db2) {\n    in x\n    out c\n    node t = dwt(wavelet=$w, levels=1)\n    x -> t\n    t -> c\n}\n"
                             "block twice {\n    in x\n    out y\n    node first = bank_level()\n    node second = bank_level(wavelet=db2)\n"
                             "    x -> first.x\n    first.y -> second.x\n    second.y -> y\n}\n";
    const auto blocks = probes_of("pipeline \"b\" v2\n" + defs + kSource +
                                  "node an = analyze()\nnode back = idwt()\nnode tw = twice()\nx -> an -> back\nx -> tw\nprobe back as \"back\"\nprobe tw as \"tw\"\n");
    const auto hand = probes_of(std::string{"pipeline \"h\" v2\n"} + kLevel + kSource +
                                "node t = dwt(wavelet=db2, levels=1)\nnode back = idwt()\nnode first = bank_level()\nnode second = bank_level(wavelet=db2)\n"
                                "x -> t -> back\nx -> first.x\nfirst.y -> second.x\nprobe back as \"back\"\nprobe second.y as \"tw\"\n");
    CHECK(same(blocks.at("back"), hand.at("back")));
    CHECK(same(blocks.at("tw"), hand.at("tw")));
    const auto graph = graph_of("pipeline \"b\" v2\n" + defs + kSource + "node tw = twice()\nx -> tw\n");
    REQUIRE(graph);
    const auto inner = std::ranges::find_if(graph->nodes, [](const GraphNode& n) { return n.id == "tw/second/h0"; });
    REQUIRE(inner != graph->nodes.end());
    CHECK(inner->instance == "tw/second");
}

TEST_CASE("a block hides document variables", "[blocks-flatten]") {
    const auto graph = graph_of(std::string{"pipeline \"b\" v2\nlet k = 2\n"} + "block f {\n    in x\n    out y\n    node g = gain(db=$k)\n    x -> g\n    g -> y\n}\n" + kSource +
                                "node c = f()\nx -> c\n");
    REQUIRE_FALSE(graph);
    CHECK_THAT(graph.error().message, ContainsSubstring("$k"));
    CHECK_THAT(graph.error().message, ContainsSubstring("c"));
    CHECK(graph.error().line == 11);
    const auto passed = graph_of(std::string{"pipeline \"b\" v2\nlet k = 2\n"} + "block f(k=0) {\n    in x\n    out y\n    node g = gain(db=$k)\n    x -> g\n    g -> y\n}\n" +
                                 kSource + "node c = f(k=$k)\nx -> c\n");
    REQUIRE(passed);
    const auto g = std::ranges::find_if(passed->nodes, [](const GraphNode& n) { return n.id == "c/g"; });
    REQUIRE(g != passed->nodes.end());
    CHECK(g->params.at("db") == "2");
}

TEST_CASE("recursion names the chain", "[blocks-flatten]") {
    const auto graph = graph_of(std::string{"pipeline \"b\" v2\n"} + "block a {\n    in x\n    out y\n    node n = b()\n    x -> n\n    n -> y\n}\n" +
                                "block b {\n    in x\n    out y\n    node n = a()\n    x -> n\n    n -> y\n}\n" + kSource + "node c = a()\nx -> c\n");
    REQUIRE_FALSE(graph);
    CHECK_THAT(graph.error().message, ContainsSubstring("a → b → a"));
}

TEST_CASE("depth over 16 is refused", "[blocks-flatten]") {
    std::string text = "pipeline \"b\" v2\nblock b0 {\n    in x\n    out y\n    node g = gain()\n    x -> g\n    g -> y\n}\n";
    for (int k = 1; k <= 17; ++k)
        text += "block b" + std::to_string(k) + " {\n    in x\n    out y\n    node n = b" + std::to_string(k - 1) + "()\n    x -> n\n    n -> y\n}\n";
    const auto ok = graph_of(text + kSource + "node c = b15()\nx -> c\n");
    INFO((ok ? std::string{} : ok.error().message));
    CHECK(ok);
    const auto deep = graph_of(text + kSource + "node c = b17()\nx -> c\n");
    REQUIRE_FALSE(deep);
    CHECK_THAT(deep.error().message, ContainsSubstring("16"));
}

TEST_CASE("a local block shadows a built-in with a note", "[blocks-flatten]") {
    const std::string text = std::string{"pipeline \"b\" v2\n"} + "block gain(db=0) {\n    in x\n    out y\n    node g = affine(scale=3)\n    x -> g\n    g -> y\n}\n" + kSource +
                             "node c = gain()\nx -> c\nprobe c as \"c\"\n";
    const auto loaded = load_sig(text);
    REQUIRE(loaded);
    REQUIRE(loaded->notes.size() == 1);
    CHECK_THAT(loaded->notes.front(), ContainsSubstring("gain"));
    const auto p = probes_of(text);
    const auto& y = std::get<std::vector<double>>(io::np_find(p.at("c"), "x")->data);
    CHECK(y.at(0) == 12.0);
}

TEST_CASE("use loads a library relative to the graph and refuses cycles", "[blocks-flatten]") {
    const auto dir = signals::test::temp_path("blocks-use");
    fs::remove_all(dir);
    fs::create_directories(dir / "lib");
    std::ofstream{dir / "lib" / "levels.sig"} << "pipeline \"levels\" v2\n" << kLevel;
    const auto p = probes_of(std::string{"pipeline \"b\" v2\nuse \"lib/levels.sig\"\n"} + kSource + "node lv = bank_level()\nx -> lv.x\nprobe lv.y as \"y\"\n", dir);
    CHECK(p.contains("y"));
    std::ofstream{dir / "lib" / "a.sig"} << "pipeline \"a\" v2\nuse \"b.sig\"\n";
    std::ofstream{dir / "lib" / "b.sig"} << "pipeline \"b\" v2\nuse \"a.sig\"\n";
    const auto cycle = load_sig("pipeline \"g\" v2\nuse \"lib/a.sig\"\n", {dir});
    REQUIRE_FALSE(cycle);
    CHECK_THAT(cycle.error().message, ContainsSubstring("a.sig"));
    std::ofstream{dir / "lib" / "nodes.sig"} << "pipeline \"n\" v2\nnode g = gain()\n";
    const auto nodes = load_sig("pipeline \"g\" v2\nuse \"lib/nodes.sig\"\n", {dir});
    REQUIRE_FALSE(nodes);
    CHECK_THAT(nodes.error().message, ContainsSubstring("nodes.sig"));
    const auto clash = load_sig(std::string{"pipeline \"g\" v2\nuse \"lib/levels.sig\"\n"} + kLevel, {dir});
    REQUIRE_FALSE(clash);
    CHECK_THAT(clash.error().message, ContainsSubstring("bank_level"));
}

TEST_CASE("paths inside a used block resolve from the library folder", "[blocks-flatten]") {
    const auto dir = signals::test::temp_path("blocks-paths");
    fs::remove_all(dir);
    fs::create_directories(dir / "lib");
    core::AudioBuffer tone;
    tone.sample_rate = 8000;
    tone.channels = {io::sine(440, 0.5, 0.0, 8000, 400)};
    REQUIRE(io::write_wav(dir / "lib" / "tone.wav", tone, {}));
    std::ofstream{dir / "lib" / "src.sig"} << "pipeline \"src\" v2\nblock tone {\n    out y\n    node w = wav(path=\"tone.wav\")\n    w -> y\n}\n";
    const auto p = probes_of("pipeline \"b\" v2\nuse \"lib/src.sig\"\nnode t = tone()\nprobe t as \"t\"\n", dir);
    CHECK(std::get<std::vector<double>>(io::np_find(p.at("t"), "x")->data).size() == 400);
}

TEST_CASE("examples do not use use", "[blocks-flatten]") {
    for (const auto& ex : examples()) {
        INFO(ex.name);
        const auto doc = parse_sig(ex.text);
        REQUIRE(doc);
        CHECK(doc->uses.empty());
    }
}

TEST_CASE("an error inside a block names the call and the definition line", "[blocks-flatten]") {
    const auto graph = graph_of(std::string{"pipeline \"b\" v2\n"} + "block f {\n    in x\n    out y\n    node g = gain(dbx=1)\n    x -> g\n    g -> y\n}\n" + kSource +
                                "node c = f()\nx -> c\n");
    REQUIRE_FALSE(graph);
    CHECK(graph.error().line == 10);
    CHECK_THAT(graph.error().message, ContainsSubstring("c (f, ") && ContainsSubstring(" 5): "));
}

TEST_CASE("port types of a block follow its inner nodes", "[blocks-flatten]") {
    const std::string defs = "block analyze(w=db2) {\n    in x\n    out c, lo\n    node t = dwt(wavelet=$w, levels=1)\n    x -> t -> c\n    t.a -> lo\n}\n"
                             "block synth {\n    in c\n    out y\n    node i = idwt()\n    c -> i -> y\n}\n"
                             "block pass {\n    in c\n    out o\n    c -> o\n}\n"
                             "block both {\n    in x\n    out y\n    node an = analyze()\n    node sy = synth()\n    x -> an.x\n    an.c -> sy -> y\n}\n";
    const std::string text = "pipeline \"b\" v2\n" + defs + kSource + "node an = analyze()\nnode p = pass()\nnode sy = synth()\nnode bo = both()\nx -> an.x\nan.c -> p -> sy\nx -> bo\n";
    const auto loaded = load_sig(text);
    REQUIRE(loaded);
    const auto types = own_block_types(loaded->doc);
    const auto type = [&](std::string_view id) { return *std::ranges::find(types, id, &BlockType::id); };
    CHECK(type("analyze").inputs.at(0).type == PortType::signal);
    CHECK(type("analyze").outputs.at(0).type == PortType::coeffs);
    CHECK(type("analyze").outputs.at(1).type == PortType::signal);
    CHECK(type("synth").inputs.at(0).type == PortType::coeffs);
    CHECK(type("synth").outputs.at(0).type == PortType::signal);
    CHECK(type("both").inputs.at(0).type == PortType::signal);
    CHECK(type("both").outputs.at(0).type == PortType::signal);
    CHECK(type("analyze").params.at(0).fallback == "db2");
    const auto collapsed = build_graph(loaded->doc, fs::temp_directory_path(), {.collapse = true});
    INFO((collapsed ? std::string{} : collapsed.error().message));
    REQUIRE(collapsed);
    CHECK(collapsed->nodes.size() == 5);
    const auto p = std::ranges::find(collapsed->nodes, "p", &GraphNode::id);
    REQUIRE(p != collapsed->nodes.end());
    CHECK(p->type->id == "pass");
    const auto broken = load_sig("pipeline \"b\" v2\n" + defs + kSource + "node sy = synth()\nx -> sy\n");
    REQUIRE(broken);
    CHECK_FALSE(build_graph(broken->doc, fs::temp_directory_path(), {.collapse = true}));
}

TEST_CASE("a call param keeps the brackets of an expression variable", "[blocks-flatten][review]") {
    const std::string amp = "block amp(db=0) {\n    in x\n    out y\n    node g = gain(db=$db)\n    x -> g -> y\n}\n"
                            "block outer(db=1) {\n    in x\n    out y\n    node i = amp(db=\"$db+1\")\n    x -> i -> y\n}\n"
                            "block player(dir=snd) {\n    out y\n    node s = wav(path=\"$dir/a.wav\")\n    s -> y\n}\n";
    const auto p = probes_of("pipeline \"p\" v2\nlet g = \"10+10\"\nlet base = my-sounds\n" + amp +
                             "node s = samples(values=\"1 2\", rate=8)\nnode c = amp(db=\"$g*2\")\nnode d = gain(db=\"$g*2\")\n"
                             "node o = outer(db=\"$g*2\")\nnode e = gain(db=\"$g*2+1\")\n"
                             "s -> c\ns -> d\ns -> o\ns -> e\nprobe c as \"c\"\nprobe d as \"d\"\nprobe o as \"o\"\nprobe e as \"e\"\n");
    CHECK(same(p.at("c"), p.at("d")));
    CHECK(same(p.at("o"), p.at("e")));
    const auto loaded = load_sig("pipeline \"p\" v2\nlet base = my-sounds\n" + amp + "node w = player(dir=$base)\n");
    REQUIRE(loaded);
    const auto flat = flatten(loaded->doc);
    REQUIRE(flat);
    const auto s = std::ranges::find(flat->nodes, "w/s", &NodeDecl::id);
    REQUIRE(s != flat->nodes.end());
    CHECK(s->params.at("path") == "my-sounds/a.wav");
}

TEST_CASE("a required input of a call must be wired", "[blocks-flatten][review]") {
    const auto doc = load_sig(std::string{"pipeline \"p\" v2\n"} +
                              "block mix {\n    in a\n    in b\n    out y\n    node m = multiply()\n    a -> m.a\n    b -> m.b\n    m -> y\n}\n" + kSource +
                              "node c = mix()\nx -> c.a\nprobe c as \"c\"\n");
    REQUIRE(doc);
    const auto flat = flatten(doc->doc);
    REQUIRE_FALSE(flat);
    CHECK_THAT(flat.error().message, ContainsSubstring("c"));
    CHECK_THAT(flat.error().message, ContainsSubstring("b"));
}

TEST_CASE("default probe names keep the port and the call's own name", "[blocks-flatten][review]") {
    const auto p = probes_of(std::string{"pipeline \"p\" v2\n"} +
                             "block split {\n    in x\n    out y\n    node p = polyphase()\n    node s = add()\n    x -> p\n    p.even -> s.a\n    p.odd -> s.b\n"
                             "    s -> y\n    probe p.even\n    probe p.odd\n}\n"
                             "block pass {\n    in x\n    out y\n    x -> y\n}\n" +
                             kSource + "node c = split()\nnode t = pass()\nx -> c\nx -> t\nprobe t\nprobe x\n");
    CHECK(p.contains("c/p.even"));
    CHECK(p.contains("c/p.odd"));
    CHECK(p.contains("t"));
    CHECK(p.contains("x"));
}

TEST_CASE("a large expansion is refused quickly and a deep one is fast", "[blocks-flatten][review]") {
    const auto chain = [](int levels, int width) {
        std::string text = "pipeline \"p\" v2\nblock b0 {\n    in x\n    out y\n    node g = gain()\n    x -> g -> y\n}\n";
        for (int k = 1; k <= levels; ++k) {
            text += "block b" + std::to_string(k) + " {\n    in x\n    out y\n";
            for (int c = 0; c < width; ++c) text += "    node c" + std::to_string(c) + " = b" + std::to_string(k - 1) + "()\n";
            text += "    x";
            for (int c = 0; c < width; ++c) text += " -> c" + std::to_string(c);
            text += " -> y\n}\n";
        }
        return text + "node s = samples(values=\"1\", rate=8)\nnode top = b" + std::to_string(levels) + "()\ns -> top\n";
    };
    const auto started = std::chrono::steady_clock::now();
    const auto big = load_sig(chain(6, 10));
    REQUIRE(big);
    const auto refused = flatten(big->doc);
    REQUIRE_FALSE(refused);
    CHECK_THAT(refused.error().message, ContainsSubstring("100000"));
    const auto deep = load_sig(chain(12, 2));
    REQUIRE(deep);
    const auto ok = flatten(deep->doc);
    INFO((ok ? std::string{} : ok.error().message));
    REQUIRE(ok);
    CHECK(ok->nodes.size() == 4097);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{3});
}

TEST_CASE("one library reached twice is loaded once", "[blocks-flatten][review]") {
    const auto dir = signals::test::temp_path("blocks-diamond");
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream{dir / "d.sig"} << "pipeline \"d\" v2\nblock amp(db=0) {\n    in x\n    out y\n    node g = gain(db=$db)\n    x -> g -> y\n}\n";
    std::ofstream{dir / "b.sig"} << "pipeline \"b\" v2\nuse \"d.sig\"\n";
    const auto both = load_sig("pipeline \"m\" v2\nuse \"d.sig\"\nuse \"b.sig\"\nuse \"d.sig\"\nnode s = samples(values=\"1\", rate=8)\nnode c = amp()\ns -> c\n", {dir});
    INFO((both ? std::string{} : both.error().message));
    REQUIRE(both);
    CHECK(std::ranges::count(both->doc.imported, std::string{"amp"}, &BlockDef::name) == 1);
    for (int k = 0; k < 24; ++k)
        std::ofstream{dir / ("l" + std::to_string(k) + ".sig")} << "pipeline \"l\" v2\nuse \"l" << k + 1 << ".sig\"\nuse \"m" << k << ".sig\"\n";
    for (int k = 0; k < 24; ++k) std::ofstream{dir / ("m" + std::to_string(k) + ".sig")} << "pipeline \"m\" v2\nuse \"l" << k + 1 << ".sig\"\n";
    std::ofstream{dir / "l24.sig"} << "pipeline \"l\" v2\nuse \"d.sig\"\n";
    const auto started = std::chrono::steady_clock::now();
    const auto deep = load_sig("pipeline \"m\" v2\nuse \"l0.sig\"\n", {dir});
    INFO((deep ? std::string{} : deep.error().message));
    REQUIRE(deep);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

TEST_CASE("a call input takes one wire and a port wired to itself is an error", "[blocks-flatten][review]") {
    const std::string pass = "block pass {\n    in x\n    out y\n    x -> y\n}\n";
    std::string many = "pipeline \"p\" v2\n" + pass;
    for (int k = 0; k < 50; ++k) many += "node s" + std::to_string(k) + " = samples(values=\"1\", rate=8)\n";
    many += "node c = pass()\n";
    for (int k = 0; k < 50; ++k) many += "s" + std::to_string(k) + " -> c\n";
    for (int k = 0; k < 50; ++k) many += "node t" + std::to_string(k) + " = gain()\nc -> t" + std::to_string(k) + "\n";
    const auto doc = load_sig(many);
    REQUIRE(doc);
    const auto flat = flatten(doc->doc);
    REQUIRE_FALSE(flat);
    CHECK_THAT(flat.error().message, ContainsSubstring("c"));
    const auto loop = load_sig("pipeline \"p\" v2\n" + pass + "node c = pass()\nc.y -> c.x\nprobe c.x as \"in\"\nprobe c as \"out\"\n");
    REQUIRE(loop);
    CHECK_FALSE(build_graph(loop->doc, fs::temp_directory_path()));
}

TEST_CASE("a block body builds as a graph with its ports as nodes", "[blocks-flatten][block-view]") {
    const auto loaded = load_sig(std::string{"pipeline \"b\" v2\n"} + kLevel +
                                 "block analyze(w=db2) {\n    in x\n    out c\n    node t = dwt(wavelet=$w, levels=1)\n    x -> t -> c\n}\n"
                                 "block outer {\n    in x\n    out y, c\n    node lv = bank_level()\n    node an = analyze()\n    x -> lv.x\n    lv.y -> y\n    x -> an -> c\n}\n");
    REQUIRE(loaded);
    const auto g = build_block_graph(loaded->doc, "bank_level", fs::temp_directory_path());
    INFO((g ? std::string{} : g.error().message));
    REQUIRE(g);
    const auto node = [&](std::string_view id) { return std::ranges::find(g->nodes, id, &GraphNode::id); };
    REQUIRE(node("x") != g->nodes.end());
    CHECK(node("x")->type->id == "in");
    CHECK(node("d")->type->id == "out");
    CHECK(node("h0")->type->id == "convolve");
    CHECK(node("h0")->params.at("filter") == "haar.h0");
    REQUIRE(node("sum")->sources.at(0));
    const auto o = build_block_graph(loaded->doc, "outer", fs::temp_directory_path());
    INFO((o ? std::string{} : o.error().message));
    REQUIRE(o);
    const auto an = std::ranges::find(o->nodes, "an", &GraphNode::id);
    REQUIRE(an != o->nodes.end());
    CHECK(an->type->id == "analyze");
    const auto c = std::ranges::find(o->nodes, "c", &GraphNode::id);
    REQUIRE(c != o->nodes.end());
    CHECK(c->type->inputs.at(0).type == PortType::coeffs);
    CHECK_FALSE(build_block_graph(loaded->doc, "missing", fs::temp_directory_path()));
}
