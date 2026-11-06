#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/design/families.hpp"
#include "core/wavedec.hpp"
#include "io/generators.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "support/paths.hpp"

using namespace signals;
using namespace signals::pipeline;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kChain = R"(node src = gen(kind=sine, freq=440, duration=0.5)
node dwt = dwt(wavelet=db2, levels=4)
node thr = threshold(mode=soft)
node rec = idwt(wavelet=db2)
src -> dwt -> thr -> rec
probe rec as "out"
probe dwt.d3 as "d3"
)";

Graph graph_of(std::string_view text, const std::filesystem::path& dir = ".") {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    auto g = build_graph(*doc, dir);
    REQUIRE(g);
    return *g;
}

const std::vector<double>& channel(const RunResult& r, const std::string& probe) {
    return std::get<core::AudioBuffer>(r.probes.at(probe)).channels.at(0);
}

}  // namespace

TEST_CASE("every node reports waiting, running and done in order", "[pipeline][executor]") {
    const auto g = graph_of(kChain);
    ResultCache cache{std::size_t{1} << 30};
    std::mutex m;
    std::map<std::string, std::vector<NodeStatus>> seen;
    const RunEvents events{[&](const NodeStatus& s) {
        std::lock_guard lock{m};
        seen[s.id].push_back(s);
    }};
    core::CancelToken cancel;
    const auto r = run_graph(g, cache, events, cancel);
    REQUIRE(r);
    for (const auto& n : g.nodes) {
        INFO(n.id);
        const auto& list = seen[n.id];
        REQUIRE(list.size() >= 3);
        CHECK(list.front().state == NodeState::waiting);
        CHECK(list[1].state == NodeState::running);
        CHECK(list.back().state == NodeState::done);
        double last = 0;
        for (const auto& s : list) {
            CHECK(s.fraction >= last);
            last = s.fraction;
        }
        CHECK(list.back().fraction == 1.0);
        CHECK(list.back().wall_seconds >= 0);
        CHECK(list.back().bytes > 0);
    }
    REQUIRE(r->nodes.size() == 4);
    CHECK(r->nodes[0].id == "src");
    CHECK(r->probes.contains("out"));
}

TEST_CASE("a second run takes every node from the cache with the same result", "[pipeline][executor][cache]") {
    const auto g = graph_of(kChain);
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    const auto first = run_graph(g, cache, {}, cancel);
    const auto second = run_graph(g, cache, {}, cancel);
    REQUIRE(first);
    REQUIRE(second);
    for (const auto& s : second->nodes) CHECK(s.state == NodeState::cached);
    CHECK(channel(*first, "out") == channel(*second, "out"));
    ResultCache fresh{std::size_t{1} << 30};
    const auto third = run_graph(g, fresh, {}, cancel);
    REQUIRE(third);
    for (const auto& s : third->nodes) CHECK(s.state == NodeState::done);
    CHECK(channel(*third, "out") == channel(*first, "out"));
}

TEST_CASE("changing threshold recomputes only it and what follows", "[pipeline][executor][cache]") {
    auto doc = parse_sig(kChain);
    REQUIRE(doc);
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    const auto before = build_graph(*doc, ".");
    REQUIRE(before);
    REQUIRE(run_graph(*before, cache, {}, cancel));
    REQUIRE(apply_set(*doc, "thr.mode=hard"));
    const auto after = build_graph(*doc, ".");
    REQUIRE(after);
    const auto r = run_graph(*after, cache, {}, cancel);
    REQUIRE(r);
    std::map<std::string, NodeState> state;
    for (const auto& s : r->nodes) state[s.id] = s.state;
    CHECK(state["src"] == NodeState::cached);
    CHECK(state["dwt"] == NodeState::cached);
    CHECK(state["thr"] == NodeState::done);
    CHECK(state["rec"] == NodeState::done);
}

TEST_CASE("a probe on dwt.d3 returns exactly band d3", "[pipeline][executor]") {
    const auto g = graph_of(kChain);
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    const auto r = run_graph(g, cache, {}, cancel);
    REQUIRE(r);
    const auto x = io::sine(440, 1, 0, 44100, 22050);
    const auto bank = core::wavelet("db2");
    REQUIRE(bank);
    const auto c = core::wavedec<double>(x, *bank, 4, {core::Extension::symmetric, false});
    REQUIRE(c);
    CHECK(channel(*r, "d3") == c->bands[2]);
    CHECK(std::get<core::AudioBuffer>(r->probes.at("d3")).sample_rate == 44100.0 / 8);
}

TEST_CASE("check reports decide whether the run passed", "[pipeline][executor]") {
    const auto g = graph_of(R"(node src = gen(duration=0.2)
node dwt = dwt(wavelet=db4, levels=3, ext=periodization)
node rec = idwt(wavelet=db4, ext=periodization)
node chk = check(kind=pr, tol=1e-9)
src -> dwt -> rec -> chk.in
src -> chk.ref
)");
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    const auto r = run_graph(g, cache, {}, cancel);
    REQUIRE(r);
    REQUIRE(r->reports.size() == 1);
    CHECK(r->checks_passed);
}

TEST_CASE("a failing node names itself and its descendants are bypassed", "[pipeline][executor]") {
    const auto g = graph_of(R"(node src = wav(path="no-such-file.wav")
node g = gain(db=-6)
src -> g
)");
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    std::mutex m;
    std::map<std::string, NodeState> last;
    const RunEvents events{[&](const NodeStatus& s) {
        std::lock_guard lock{m};
        last[s.id] = s.state;
    }};
    const auto r = run_graph(g, cache, events, cancel);
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("src") != std::string::npos);
    CHECK(last["src"] == NodeState::failed);
    CHECK(last["g"] == NodeState::bypassed);
}

TEST_CASE("cancel stops a ten-minute run quickly and leaves no file", "[pipeline][executor][cancel]") {
    const auto dir = test::temp_path("cancel-run");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto g = graph_of(R"(node src = gen(kind=sine, duration=600)
node dwt = dwt(wavelet=db8, levels=8)
node rec = idwt(wavelet=db8)
node out = wav_out(path="long.wav")
src -> dwt -> rec -> out
)",
                            dir);
    ResultCache cache{std::size_t{1} << 30};
    core::CancelToken cancel;
    auto future = std::async(std::launch::async, [&] { return run_graph(g, cache, {}, cancel); });
    std::this_thread::sleep_for(150ms);
    const auto asked = std::chrono::steady_clock::now();
    cancel.cancel();
    const auto r = future.get();
    const auto took = std::chrono::steady_clock::now() - asked;
    CHECK(took < 200ms);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == core::Errc::cancelled);
    CHECK(std::filesystem::is_empty(dir));
}

TEST_CASE("the cache evicts the oldest results over its budget", "[pipeline][cache]") {
    const auto values = [](std::size_t n) {
        core::AudioBuffer a;
        a.sample_rate = 1;
        a.channels.push_back(std::vector<double>(n, 1.0));
        return std::make_shared<const std::vector<Value>>(std::vector<Value>{std::move(a)});
    };
    ResultCache cache{1000 * sizeof(double)};
    cache.put(1, values(400));
    cache.put(2, values(400));
    REQUIRE(cache.get(1));
    cache.put(3, values(400));
    CHECK(cache.get(1));
    CHECK_FALSE(cache.get(2));
    CHECK(cache.get(3));
    CHECK(cache.bytes() <= 1000 * sizeof(double));
    cache.put(4, values(2000));
    CHECK_FALSE(cache.get(4));
}
