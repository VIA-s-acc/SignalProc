#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

#include "core/metrics.hpp"
#include "core/pitch.hpp"
#include "core/tempo.hpp"
#include "pipeline/block.hpp"
#include "support/golden.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

core::Result<std::vector<Value>> attempt(std::string_view type, std::vector<Value> inputs, const Params& params = {}) {
    const auto* t = find_block_type(type);
    REQUIRE(t);
    auto resolved = resolve_params(*t, params);
    INFO(type << ": " << (resolved ? std::string{} : resolved.error().message));
    REQUIRE(resolved);
    RunContext ctx{std::filesystem::temp_directory_path()};
    return t->run(ctx, inputs, *resolved);
}

const core::AudioBuffer& audio(const Value& v) { return std::get<core::AudioBuffer>(v); }

core::AudioBuffer run_audio(std::string_view type, core::AudioBuffer in, const Params& params = {}) {
    auto out = attempt(type, {Value{std::move(in)}}, params);
    INFO(type << ": " << (out ? std::string{} : out.error().message));
    REQUIRE(out);
    return audio(out->at(0));
}

std::vector<double> tone(double f0, double fs, std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        x[i] = std::sin(2 * std::numbers::pi * f0 * t) + 0.4 * std::sin(2 * std::numbers::pi * 2 * f0 * t + 0.3);
    }
    return x;
}

double pitch(const std::vector<double>& x, double fs) {
    const auto track = core::yin_track(x, fs, {});
    REQUIRE(track);
    return core::median_f0(*track);
}

double cents(double f, double reference) { return 1200 * std::log2(f / reference); }

}  // namespace

TEST_CASE("time_stretch runs the core stretch on every channel and keeps the rate", "[pipeline][tempo]") {
    constexpr double fs = 22050;
    const auto left = tone(220, fs, 22050), right = tone(330, fs, 22050);
    for (const std::string method : {"vocoder", "locked", "wsola"}) {
        INFO(method);
        const auto out = run_audio("time_stretch", {fs, {left, right}}, {{"rate", "0.8"}, {"method", method}, {"n_fft", "1024"}, {"hop", "256"}});
        CHECK(out.sample_rate == fs);
        REQUIRE(out.channels.size() == 2);
        const auto o = core::StretchOptions{*core::parse_stretch_method(method), 1024, 256};
        CHECK(core::max_abs_error(out.channels[0], *core::time_stretch(left, 0.8, o)) == 0.0);
        CHECK(core::max_abs_error(out.channels[1], *core::time_stretch(right, 0.8, o)) == 0.0);
        CHECK(out.channels[0].size() == 27562);
        CHECK(std::abs(cents(pitch(out.channels[1], fs), 330)) < 1);
    }
}

TEST_CASE("pitch_shift moves the pitch and keeps the length, speed moves both", "[pipeline][tempo]") {
    constexpr double fs = 22050;
    const auto x = tone(220, fs, 22050);
    const auto up = run_audio("pitch_shift", {fs, {x}}, {{"semitones", "12"}});
    REQUIRE(up.channels[0].size() == x.size());
    CHECK(std::abs(cents(pitch(up.channels[0], fs), 440)) < 1);
    const auto fifth = run_audio("pitch_shift", {fs, {x}}, {{"semitones", "7"}, {"method", "wsola"}});
    CHECK(std::abs(cents(pitch(fifth.channels[0], fs), 220 * std::pow(2.0, 7.0 / 12))) < 1);
    const auto fast = run_audio("speed", {fs, {x}}, {{"rate", "2"}});
    CHECK(fast.sample_rate == fs);
    CHECK(fast.channels[0].size() == x.size() / 2);
    CHECK(std::abs(cents(pitch(fast.channels[0], fs), 440)) < 1);
}

TEST_CASE("tempo blocks pass empty channels through and explain a bad window", "[pipeline][tempo]") {
    const core::AudioBuffer empty{8000, {{}}};
    for (const std::string type : {"time_stretch", "pitch_shift", "speed"}) CHECK(run_audio(type, empty).channels.at(0).empty());
    const auto odd = attempt("time_stretch", {Value{core::AudioBuffer{8000, {tone(100, 8000, 4000)}}}}, {{"n_fft", "1023"}});
    REQUIRE_FALSE(odd);
    CHECK(odd.error().message.find("1023") != std::string::npos);
    const auto wide = attempt("pitch_shift", {Value{core::AudioBuffer{8000, {tone(100, 8000, 4000)}}}}, {{"n_fft", "512"}, {"hop", "300"}});
    REQUIRE_FALSE(wide);
    CHECK(wide.error().message.find("300") != std::string::npos);
}

TEST_CASE("tempo blocks refuse an output longer than the sample cap", "[pipeline][tempo][review]") {
    CHECK(checked_scaled_length(1000, 10.0) == 10000u);
    CHECK(checked_scaled_length(1001, 0.5) == 500u);
    const auto huge = checked_scaled_length(100'000'000, 10.0);
    REQUIRE_FALSE(huge);
    CHECK(huge.error().message.find("100000000") != std::string::npos);
    CHECK_FALSE(checked_scaled_length(200'000'000, 4.0));
}

namespace {

struct Recorder final : core::ProgressSink {
    std::vector<double> seen;
    void report(double fraction) override { seen.push_back(fraction); }
    bool cancelled() const override { return false; }
};

}  // namespace

TEST_CASE("the pitch block reports progress once across all channels", "[pipeline][tempo][review]") {
    const auto* t = find_block_type("pitch");
    REQUIRE(t);
    auto params = resolve_params(*t, {});
    REQUIRE(params);
    Recorder progress;
    RunContext ctx{std::filesystem::temp_directory_path()};
    ctx.progress = &progress;
    const std::vector<Value> in{core::AudioBuffer{22050, {tone(220, 22050, 22050), tone(330, 22050, 22050)}}};
    REQUIRE(t->run(ctx, in, *params));
    REQUIRE(progress.seen.size() > 2);
    CHECK(std::is_sorted(progress.seen.begin(), progress.seen.end()));
    CHECK(progress.seen.back() > 0.5);
}
