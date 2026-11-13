#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"
#include "learn/learn_support.hpp"

using namespace signals::pipeline;

namespace {

signals::core::Result<Params> resolve(std::string_view block, Params given) {
    const auto* t = find_block_type(block);
    REQUIRE(t != nullptr);
    return resolve_params(*t, given);
}

}  // namespace

TEST_CASE("real and integer parameters accept expressions", "[pipeline][params]") {
    auto gain = resolve("gain", {{"db", "20*log10(2)"}});
    REQUIRE(gain.has_value());
    CHECK(param_real(*gain, "db") == Catch::Approx(20 * std::log10(2.0)));
    auto down = resolve("downsample", {{"m", "2^2"}});
    REQUIRE(down.has_value());
    CHECK(param_int(*down, "m") == 4);
    auto plain = resolve("gain", {{"db", "-6"}});
    REQUIRE(plain.has_value());
    CHECK(param_text(*plain, "db") == "-6");
}

TEST_CASE("an integer expression must give an integer and stay in bounds", "[pipeline][params]") {
    CHECK_FALSE(resolve("downsample", {{"m", "3/2"}}).has_value());
    CHECK_FALSE(resolve("downsample", {{"m", "2^20"}}).has_value());
    CHECK_FALSE(resolve("gain", {{"db", "1 +"}}).has_value());
}

TEST_CASE("an expression parameter changes the run and the original text is kept on save", "[pipeline][params]") {
    const auto doc = signals::test::sig_doc(R"sig(pipeline "t" v1
node x = samples(values="1 2", rate=8)
node y = gain(db="20*log10(2)")
x -> y
probe y as "y"
)sig");
    const auto r = signals::test::run_doc(doc);
    const auto& y = std::get<signals::core::AudioBuffer>(r.probes.at("y"));
    CHECK(y.channels[0][1] == Catch::Approx(4.0));
    CHECK(write_sig(doc).find("20*log10(2)") != std::string::npos);
}
