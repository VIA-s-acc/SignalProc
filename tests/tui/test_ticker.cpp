#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "tui/shell/ticker.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

TEST_CASE("ticker notifies only while wanted", "[tui][ticker]") {
    std::atomic<int> n{0};
    Ticker t{[&] { ++n; }, 5ms};
    std::this_thread::sleep_for(30ms);
    CHECK(n == 0);
    CHECK_FALSE(t.active());
    t.want("x", true);
    CHECK(t.active());
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (n < 3 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(5ms);
    CHECK(n >= 3);
    t.want("y", true);
    t.want("x", false);
    CHECK(t.active());
    t.want("y", false);
    CHECK_FALSE(t.active());
    std::this_thread::sleep_for(10ms);
    const int after = n;
    std::this_thread::sleep_for(30ms);
    CHECK(n <= after + 1);
}

TEST_CASE("a ticker stops cleanly while ticking", "[tui][ticker]") {
    std::atomic<int> n{0};
    {
        Ticker t{[&] { ++n; }, 1ms};
        t.want("x", true);
        std::this_thread::sleep_for(10ms);
    }
    const int after = n;
    std::this_thread::sleep_for(10ms);
    CHECK(n == after);
}

TEST_CASE("begin_frame drops wants that are not renewed", "[tui][ticker]") {
    Ticker t{[] {}, 5ms};
    t.want("a", true);
    CHECK(t.active());
    t.begin_frame();
    CHECK_FALSE(t.active());
    t.want("a", true);
    CHECK(t.active());
}
