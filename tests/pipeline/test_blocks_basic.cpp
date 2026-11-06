#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <span>
#include <vector>

#include "core/signal_ops.hpp"
#include "io/generators.hpp"
#include "io/wav.hpp"
#include "pipeline/block.hpp"
#include "support/paths.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

core::AudioBuffer stereo() {
    core::AudioBuffer a;
    a.sample_rate = 8000;
    a.channels = {{0.5, -0.25, 1.0, 0.0, 0.75, 0.1}, {0.1, 0.2, -0.3, 0.4, -0.5, 0.6}};
    return a;
}

std::vector<Value> run(std::string_view type, std::vector<Value> inputs, Params params = {}) {
    const auto* t = find_block_type(type);
    REQUIRE(t);
    auto resolved = resolve_params(*t, params);
    REQUIRE(resolved);
    RunContext ctx{std::filesystem::path{SIGNALS_SOURCE_DIR}};
    auto out = t->run(ctx, inputs, *resolved);
    INFO(type << ": " << (out ? std::string{} : out.error().message));
    REQUIRE(out);
    return *out;
}

const core::AudioBuffer& audio(const Value& v) { return std::get<core::AudioBuffer>(v); }

}  // namespace

TEST_CASE("params get defaults and reject bad values", "[pipeline]") {
    const auto* gen = find_block_type("gen");
    REQUIRE(gen);
    auto p = resolve_params(*gen, {});
    REQUIRE(p);
    CHECK(p->at("kind") == "sine");
    CHECK_FALSE(resolve_params(*gen, {{"kind", "foo"}}));
    CHECK(resolve_params(*gen, {{"kind", "foo"}}).error().message.find("kind") != std::string::npos);
    CHECK_FALSE(resolve_params(*find_block_type("gain"), {{"db", "abc"}}));
    CHECK_FALSE(resolve_params(*gen, {{"nonsense", "1"}}));
    CHECK(find_block_type("nope") == nullptr);
}

TEST_CASE("the wav block reads test.wav like io::read_wav", "[pipeline]") {
    const auto direct = io::read_wav(signals::test::sample("test.wav"));
    REQUIRE(direct);
    const auto out = run("wav", {}, {{"path", "resources/samples/test.wav"}});
    REQUIRE(out.size() == 1);
    CHECK(audio(out[0]).channels == direct->audio.channels);
    CHECK(audio(out[0]).sample_rate == direct->audio.sample_rate);
}

TEST_CASE("the generator matches io::sine", "[pipeline]") {
    const auto out = run("gen", {}, {{"kind", "sine"}, {"freq", "440"}, {"rate", "8000"}, {"duration", "0.01"}});
    const auto expected = io::sine(440, 1.0, 0.0, 8000, 80);
    REQUIRE(audio(out[0]).channels.size() == 1);
    CHECK(audio(out[0]).channels[0] == expected);
}

TEST_CASE("element-wise blocks match core::signal_ops", "[pipeline]") {
    const auto in = stereo();
    CHECK(audio(run("gain", {in}, {{"db", "-6"}})[0]).channels[1] == core::gain_db(in.channels[1], -6));
    CHECK(audio(run("normalize", {in}, {{"kind", "peak"}, {"target_dbfs", "-1"}})[0]).channels[0] == core::normalize_peak(in.channels[0], -1));
    CHECK(audio(run("normalize", {in}, {{"kind", "rms"}, {"target_dbfs", "-20"}})[0]).channels[0] == core::normalize_rms(in.channels[0], -20));
    CHECK(audio(run("remove_dc", {in})[0]).channels[1] == core::remove_dc(in.channels[1]));
}

TEST_CASE("channel blocks match core::signal_ops", "[pipeline]") {
    const auto in = stereo();
    const auto split = run("split", {in}, {{"mode", "lr"}});
    REQUIRE(split.size() == 2);
    CHECK(audio(split[0]).channels == std::vector<std::vector<double>>{in.channels[0]});
    CHECK(audio(split[1]).channels == std::vector<std::vector<double>>{in.channels[1]});
    CHECK(audio(run("split", {in}, {{"mode", "channel"}, {"channel", "1"}})[0]).channels == std::vector<std::vector<double>>{in.channels[1]});
    CHECK(audio(run("mixdown", {in})[0]).channels == core::mixdown(in).channels);
    const auto ms = run("mid_side", {in}, {{"direction", "encode"}});
    CHECK(audio(ms[0]).channels == core::to_mid_side(in)->channels);
    CHECK(audio(run("mid_side", {ms[0]}, {{"direction", "decode"}})[0]).channels[0][2] == Catch::Approx(in.channels[0][2]));
}

TEST_CASE("time blocks take samples or seconds", "[pipeline]") {
    const auto in = stereo();
    CHECK(audio(run("delay", {in}, {{"samples", "2"}})[0]).channels[0] == core::delay(in.channels[0], 2));
    CHECK(audio(run("reverse", {in})[0]).channels[1] == core::reverse(in.channels[1]));
    CHECK(audio(run("cut", {in}, {{"begin", "1"}, {"end", "4"}})[0]).channels[0] == *core::cut(in.channels[0], 1, 4));
    CHECK(audio(run("cut", {in}, {{"begin", "0.000125s"}, {"end", "0.0005s"}})[0]).channels[0] == *core::cut(in.channels[0], 1, 4));
    const auto* cut = find_block_type("cut");
    auto bad = cut->run(RunContext{}, std::vector<Value>{in}, *resolve_params(*cut, {{"begin", "5"}, {"end", "2"}}));
    CHECK_FALSE(bad);
}

TEST_CASE("value keys change with content", "[pipeline]") {
    auto a = stereo();
    const auto k = value_key(a);
    CHECK(k == value_key(stereo()));
    a.channels[0][0] += 1e-12;
    CHECK(k != value_key(a));
    CHECK(type_of(Value{2.0}) == PortType::scalar);
}
