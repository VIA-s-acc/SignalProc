#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/block.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

core::AudioBuffer tone(std::size_t frames, double rate, double f, double a, double phase) {
    core::AudioBuffer b;
    b.sample_rate = rate;
    std::vector<double> c(frames);
    for (std::size_t n = 0; n < frames; ++n)
        c[n] = a * std::sin(2 * M_PI * f * static_cast<double>(n) / rate + phase) + 0.2 * std::sin(2 * M_PI * 3.7 * f * static_cast<double>(n) / rate);
    b.channels.push_back(std::move(c));
    return b;
}

core::AudioBuffer join(const core::AudioBuffer& a, const core::AudioBuffer& b) {
    auto out = a;
    out.channels.push_back(b.channels.front());
    return out;
}

core::AudioBuffer channel(const core::AudioBuffer& a, std::size_t k) {
    core::AudioBuffer out;
    out.sample_rate = a.sample_rate;
    out.channels.push_back(a.channels[k]);
    return out;
}

core::Result<std::vector<Value>> attempt(const BlockType& t, std::vector<Value> inputs) {
    auto params = resolve_params(t, {});
    if (!params) return std::unexpected{params.error()};
    RunContext ctx{std::filesystem::temp_directory_path()};
    return t.run(ctx, inputs, *params);
}

double worst(const core::AudioBuffer& a, const core::AudioBuffer& b) {
    if (a.channels.size() != b.channels.size()) return INFINITY;
    double w = 0;
    for (std::size_t k = 0; k < a.channels.size(); ++k) {
        if (a.channels[k].size() != b.channels[k].size()) return INFINITY;
        for (std::size_t n = 0; n < a.channels[k].size(); ++n) w = std::max(w, std::abs(a.channels[k][n] - b.channels[k][n]));
    }
    return w;
}

}  // namespace

TEST_CASE("samples splits channels at a bar and keeps semicolons and commas inside a channel", "[pipeline][multichannel]") {
    const auto* t = find_block_type("samples");
    REQUIRE(t);
    const auto run_with = [&](std::string values) {
        auto params = resolve_params(*t, {{"values", values}, {"rate", "8"}});
        REQUIRE(params);
        RunContext ctx{std::filesystem::temp_directory_path()};
        return t->run(ctx, {}, *params);
    };
    const auto two = run_with("1 2 3 | 4;5,6");
    REQUIRE(two);
    const auto& a = std::get<core::AudioBuffer>(two->front());
    REQUIRE(a.channels.size() == 2);
    CHECK(a.channels[0] == std::vector<double>{1, 2, 3});
    CHECK(a.channels[1] == std::vector<double>{4, 5, 6});
    CHECK(a.sample_rate == 8);
    const auto uneven = run_with("1 2 | 3");
    REQUIRE_FALSE(uneven);
    CHECK(uneven.error().message.find('2') != std::string::npos);
    CHECK(uneven.error().message.find('1') != std::string::npos);
    CHECK_FALSE(run_with("1 2 |"));
    const auto one = run_with("1; 2, 3");
    REQUIRE(one);
    CHECK(std::get<core::AudioBuffer>(one->front()).channels.size() == 1);
}

TEST_CASE("every signal block treats stereo as two channels unless it mixes them on purpose", "[pipeline][multichannel]") {
    const std::map<std::string, std::string, std::less<>> mixing{
        {"split", "keeps or picks channels"},       {"mixdown", "averages the channels"},
        {"mid_side", "rotates L and R"},            {"swap", "exchanges channels 0 and 1"},
        {"pan", "places a mono mix in stereo"},     {"merge", "joins the channels of two inputs"},
        {"duplicate", "repeats the channels"},      {"select_channel", "keeps one channel"},
        {"stereo_width", "scales the side signal"}, {"balance", "weights L against R"},
        {"trim_silence", "trims the silence common to all channels"},
        {"compressor", "linked detector over all channels"},
        {"limiter", "linked detector over all channels"},
        {"gate", "linked detector over all channels"},
        {"add_noise", "independent noise per channel"},
        {"quantize", "dither seeded per channel"},
    };
    for (const auto& [id, why] : mixing) {
        INFO(id << ": " << why);
        CHECK(find_block_type(id));
    }
    const double rate = 8000;
    const auto stereo = join(tone(2048, rate, 440, 0.8, 0.0), tone(2048, rate, 660, 0.5, 1.0));
    std::size_t compared = 0;
    for (const auto& t : block_types()) {
        if (t.inputs.empty() || t.outputs.empty() || t.outputs.front().type != PortType::signal || t.side_effect || mixing.contains(t.id)) continue;
        if (!std::all_of(t.inputs.begin(), t.inputs.end(), [](const PortSpec& p) { return p.type == PortType::signal; })) continue;
        INFO(t.id);
        auto whole = attempt(t, std::vector<Value>(t.inputs.size(), Value{stereo}));
        auto first = attempt(t, std::vector<Value>(t.inputs.size(), Value{channel(stereo, 0)}));
        if (!whole) {
            CHECK_FALSE(first);
            continue;
        }
        auto second = attempt(t, std::vector<Value>(t.inputs.size(), Value{channel(stereo, 1)}));
        REQUIRE(first);
        REQUIRE(second);
        auto expected = std::get<core::AudioBuffer>(first->front());
        expected.channels.push_back(std::get<core::AudioBuffer>(second->front()).channels.at(0));
        CHECK(worst(std::get<core::AudioBuffer>(whole->front()), expected) <= 1e-9);
        ++compared;
    }
    CHECK(compared >= 70);
}

TEST_CASE("transforms keep every channel in their coefficients", "[pipeline][multichannel]") {
    const auto stereo = join(tone(2048, 8000, 440, 0.8, 0.0), tone(2048, 8000, 660, 0.5, 1.0));
    std::size_t seen = 0;
    for (const auto& t : block_types()) {
        if (t.inputs.size() != 1 || t.inputs.front().type != PortType::signal || t.outputs.empty() || t.outputs.front().type != PortType::coeffs) continue;
        INFO(t.id);
        auto out = attempt(t, {Value{stereo}});
        REQUIRE(out);
        CHECK(std::get<Coeffs>(out->front()).channels.size() == 2);
        ++seen;
    }
    CHECK(seen >= 5);
}
