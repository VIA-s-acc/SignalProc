#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "tui/monitor/monitor.hpp"
#include "tui/monitor/sampler.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

namespace {

void touch(std::vector<char>& v) {
    for (std::size_t i = 0; i < v.size(); i += 4096) v[i] = static_cast<char>(i);
}

double ps_rss_kib(pid_t pid) {
    const std::string cmd = "ps -o rss= -p " + std::to_string(pid);
    FILE* f = popen(cmd.c_str(), "r");
    REQUIRE(f);
    char buf[64] = {};
    const bool ok = std::fgets(buf, sizeof buf, f) != nullptr;
    pclose(f);
    REQUIRE(ok);
    return std::stod(buf);
}

double clock_seconds(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

}  // namespace

TEST_CASE("resident memory agrees with ps within 5 percent", "[tui][monitor]") {
    std::vector<char> ballast(64 << 20, 1);
    touch(ballast);
    auto s = sample_process();
    REQUIRE(s);
    const double ps_kib = ps_rss_kib(getpid());
    CHECK(std::abs(static_cast<double>(s->rss_bytes) / 1024.0 - ps_kib) <= 0.05 * ps_kib);
    CHECK(s->peak_rss_bytes >= s->rss_bytes);
    CHECK(s->rss_bytes > (64u << 20));
}

TEST_CASE("cpu load of a busy thread is about one core", "[tui][monitor]") {
    auto prev = sample_process();
    const double c0 = clock_seconds(CLOCK_PROCESS_CPUTIME_ID), w0 = clock_seconds(CLOCK_MONOTONIC);
    std::atomic<bool> stop{false};
    std::thread busy{[&] { while (!stop) {} }};
    std::this_thread::sleep_for(1s);
    stop = true;
    busy.join();
    auto cur = sample_process();
    const double c1 = clock_seconds(CLOCK_PROCESS_CPUTIME_ID), w1 = clock_seconds(CLOCK_MONOTONIC);
    REQUIRE(prev);
    REQUIRE(cur);
    const double truth = (c1 - c0) / (w1 - w0) * 100;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const auto snap = derive(*prev, *cur, cores, 0);
    CHECK(std::abs(snap.cpu_one_core_pct - truth) <= 5.0);
    CHECK(snap.cpu_one_core_pct > 80.0);
    CHECK(std::abs(snap.cpu_all_cores_pct - snap.cpu_one_core_pct / cores) < 1e-9);
    CHECK(snap.core_load_pct.size() == cur->core_busy_ticks.size());
}

TEST_CASE("thread count follows started threads", "[tui][monitor]") {
    const auto before = sample_process()->threads;
    std::atomic<bool> stop{false};
    std::thread a{[&] { while (!stop) std::this_thread::sleep_for(1ms); }};
    std::thread b{[&] { while (!stop) std::this_thread::sleep_for(1ms); }};
    const auto during = sample_process()->threads;
    stop = true;
    a.join();
    b.join();
    CHECK(during == before + 2);
}

TEST_CASE("sampling costs under half a percent at one hertz", "[tui][monitor]") {
    const double t0 = clock_seconds(CLOCK_THREAD_CPUTIME_ID);
    for (int k = 0; k < 100; ++k) REQUIRE(sample_process());
    const double per_call = (clock_seconds(CLOCK_THREAD_CPUTIME_ID) - t0) / 100;
    CHECK(per_call < 0.005);
}

TEST_CASE("history keeps five minutes", "[tui][monitor]") {
    ResourceMonitor m{[] {}, 1ms};
    m.set_queue_probe([] { return std::size_t{3}; });
    for (int k = 0; k < 5000 && m.samples() < 400; ++k) std::this_thread::sleep_for(1ms);
    REQUIRE(m.samples() >= 400);
    CHECK(m.history().size() == 300);
    CHECK(m.latest().queued_jobs == 3);
    CHECK(m.latest().rss > 0);
}

TEST_CASE("the monitor stops without waiting a full period", "[tui][monitor]") {
    const auto t0 = std::chrono::steady_clock::now();
    { ResourceMonitor m{[] {}, 10s}; }
    CHECK(std::chrono::steady_clock::now() - t0 < 500ms);
}
