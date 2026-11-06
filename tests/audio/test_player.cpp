#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <numeric>
#include <thread>
#include <vector>

#include "audio/manual_sink.hpp"
#include "audio/player.hpp"

using namespace signals::audio;
using signals::test::FailingSink;
using signals::test::ManualSink;
using namespace std::chrono_literals;

namespace {

std::vector<float> ramp(std::size_t n) {
    std::vector<float> v(n);
    std::iota(v.begin(), v.end(), 0.0f);
    return v;
}

Clip mono(const std::vector<float>& ch, std::string tag = "t") { return {{ch}, 48000, nullptr, std::move(tag)}; }

}  // namespace

TEST_CASE("playing pulls the clip from the start frame", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto ch = ramp(1000);
    REQUIRE(p.play(mono(ch), 100));
    CHECK(raw->rate() == 48000);
    CHECK(raw->channels() == 1);
    raw->pump(64);
    CHECK(raw->last.front() == 100.0f);
    CHECK(raw->last.back() == 163.0f);
    CHECK(p.position() == 164);
    CHECK(p.state() == PlayState::playing);
    CHECK(p.tag() == "t");
}

TEST_CASE("playback stops at the end frame", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto ch = ramp(1000);
    REQUIRE(p.play(mono(ch), 0, 10));
    raw->pump(64);
    CHECK(raw->last[9] == 9.0f);
    for (std::size_t i = 10; i < 64; ++i) CHECK(raw->last[i] == 0.0f);
    CHECK(p.position() == 10);
    p.poll();
    CHECK(p.state() == PlayState::finished);
    CHECK_FALSE(raw->running);
}

TEST_CASE("playback stops at the end of the clip", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto ch = ramp(20);
    REQUIRE(p.play(mono(ch), 15));
    raw->pump(16);
    CHECK(raw->last[4] == 19.0f);
    CHECK(raw->last[5] == 0.0f);
    p.poll();
    CHECK(p.state() == PlayState::finished);
}

TEST_CASE("stereo clips are interleaved", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const std::vector<float> l(8, 1.0f), r(8, -1.0f);
    REQUIRE(p.play({{l, r}, 44100, nullptr, "s"}, 0));
    CHECK(raw->channels() == 2);
    raw->pump(4);
    CHECK(raw->last == std::vector<float>{1, -1, 1, -1, 1, -1, 1, -1});
}

TEST_CASE("only the first two of many channels play", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const std::vector<float> a(4, 1.0f), b(4, 2.0f), c(4, 3.0f);
    REQUIRE(p.play({{a, b, c}, 44100, nullptr, "s"}, 0));
    CHECK(raw->channels() == 2);
    raw->pump(1);
    CHECK(raw->last == std::vector<float>{1, 2});
}

TEST_CASE("replace keeps the position for A/B", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto a = ramp(1000);
    std::vector<float> b(1000);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = -static_cast<float>(i);
    REQUIRE(p.play(mono(a, "a"), 0));
    raw->pump(100);
    p.replace(mono(b, "b"));
    CHECK(p.position() == 100);
    CHECK(p.tag() == "b");
    raw->pump(2);
    CHECK(raw->last == std::vector<float>{-100, -101});
    const auto shorter = ramp(50);
    p.replace(mono(shorter, "c"));
    CHECK(p.position() == 50);
}

TEST_CASE("stop silences and a new play restarts the sink", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto ch = ramp(100);
    REQUIRE(p.play(mono(ch), 0));
    p.stop();
    CHECK(p.state() == PlayState::stopped);
    CHECK_FALSE(raw->running);
    REQUIRE(p.play(mono(ch), 50));
    CHECK(raw->running);
    raw->pump(1);
    CHECK(raw->last[0] == 50.0f);
}

TEST_CASE("a failing device leaves the player unavailable with a reason", "[audio]") {
    Player p{std::unique_ptr<Sink>{std::make_unique<FailingSink>()}};
    const std::vector<float> ch(10);
    CHECK_FALSE(p.play(mono(ch), 0));
    CHECK(p.state() == PlayState::unavailable);
    REQUIRE(p.unavailable_reason());
    CHECK(p.unavailable_reason()->find("no output device") != std::string::npos);
}

TEST_CASE("a player without a sink is unavailable", "[audio]") {
    Player p{signals::core::make_error(signals::core::Errc::io_error, "backend missing")};
    CHECK(p.state() == PlayState::unavailable);
    const std::vector<float> ch(10);
    CHECK_FALSE(p.play(mono(ch), 0));
    CHECK(*p.unavailable_reason() == "backend missing");
}

TEST_CASE("empty clips and a start past the end are rejected", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const std::vector<float> empty, ch(10);
    CHECK_FALSE(p.play(mono(empty), 0));
    CHECK_FALSE(p.play(mono(ch), 10));
    CHECK(p.state() == PlayState::stopped);
}

TEST_CASE("play and stop race the audio thread safely", "[audio]") {
    ManualSink* raw = nullptr;
    auto p = signals::test::manual_player(raw);
    const auto a = ramp(48000), b = ramp(48000);
    std::atomic<bool> done{false};
    std::thread audio{[&] {
        std::vector<float> buffer(64);
        while (!done) {
            if (raw->running) raw->pump(32);
            std::this_thread::yield();
        }
    }};
    const auto until = std::chrono::steady_clock::now() + 200ms;
    int k = 0;
    while (std::chrono::steady_clock::now() < until) {
        switch (k++ % 3) {
        case 0: (void)p.play(mono(a, "a"), static_cast<std::size_t>(k) % 1000); break;
        case 1: p.replace(mono(b, "b")); break;
        default: p.stop(); break;
        }
        p.poll();
    }
    done = true;
    audio.join();
    CHECK(k > 10);
}

TEST_CASE("the real device plays a short beep", "[audio][.device]") {
    auto sink = make_device_sink();
    REQUIRE(sink);
    Player p{std::move(sink)};
    std::vector<float> tone(9600);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.2f * static_cast<float>(std::sin(2 * std::numbers::pi * 440.0 * static_cast<double>(i) / 48000.0));
    REQUIRE(p.play(mono(tone), 0));
    std::this_thread::sleep_for(400ms);
    p.poll();
    CHECK(p.state() == PlayState::finished);
}
