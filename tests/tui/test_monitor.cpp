#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
#endif

#include "core/thread_name.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/monitor/sampler.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

namespace {

void touch(std::vector<char>& v) {
    // Pseudo-random bytes through a volatile pointee: the macOS memory compressor squeezes a constant ballast under load,
    // and GCC drops plain stores to memory that is freed before it is read.
    volatile char* data = v.data();
    std::uint64_t x = 0x9E3779B97F4A7C15ULL;
    for (std::size_t i = 0; i < v.size(); ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        data[i] = static_cast<char>(x);
    }
}

#if defined(__APPLE__)
double top_memory_kib(pid_t pid) {
    rusage_info_v4 ri{};
    REQUIRE(proc_pid_rusage(pid, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&ri)) == 0);
    return static_cast<double>(ri.ri_phys_footprint) / 1024.0;
}
#else
double top_memory_kib(pid_t pid) {
    const std::string cmd = "ps -o rss= -p " + std::to_string(pid);
    FILE* f = popen(cmd.c_str(), "r");
    REQUIRE(f);
    char buf[64] = {};
    const bool ok = std::fgets(buf, sizeof buf, f) != nullptr;
    pclose(f);
    REQUIRE(ok);
    return std::stod(buf);
}
#endif

double clock_seconds(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

}  // namespace

TEST_CASE("process memory agrees with top within 5 percent", "[tui][monitor]") {
    std::vector<char> ballast(64 << 20, 1);
    touch(ballast);
    auto s = sample_process();
    REQUIRE(s);
    const double top_kib = top_memory_kib(getpid());
    CHECK(std::abs(static_cast<double>(s->rss_bytes) / 1024.0 - top_kib) <= 0.05 * top_kib);
    CHECK(s->peak_rss_bytes >= s->rss_bytes);
    CHECK(s->rss_bytes > (48u << 20));
}

TEST_CASE("cpu load of a busy thread is about one core", "[tui][monitor]") {
    auto prev = sample_process();
    const double c0 = clock_seconds(CLOCK_PROCESS_CPUTIME_ID), w0 = clock_seconds(CLOCK_MONOTONIC);
    std::atomic<bool> stop{false};
    std::thread busy{[&] { while (!stop) {} }};
    std::this_thread::sleep_for(1s);
    // Sample before joining: macOS adds a joined thread's CPU time to the process a moment later.
    auto cur = sample_process();
    const double c1 = clock_seconds(CLOCK_PROCESS_CPUTIME_ID), w1 = clock_seconds(CLOCK_MONOTONIC);
    stop = true;
    busy.join();
    REQUIRE(prev);
    REQUIRE(cur);
    const double truth = (c1 - c0) / (w1 - w0) * 100;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const auto snap = derive(*prev, *cur, cores, 0);
    CHECK(std::abs(snap.cpu_one_core_pct - truth) <= 5.0);
    CHECK(snap.cpu_one_core_pct > 50.0);
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

TEST_CASE("threads are sampled with their names and cpu time", "[tui][monitor]") {
    std::atomic<bool> stop{false}, named{false};
    std::thread busy{[&] {
        signals::core::set_thread_name("signals-test");
        named = true;
        volatile double x = 0;
        while (!stop) x = x + 1;
    }};
    while (!named) std::this_thread::yield();
    const auto a = signals::tui::sample_process();
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    const auto b = signals::tui::sample_process();
    stop = true;
    busy.join();
    REQUIRE(a);
    REQUIRE(b);
#if defined(__APPLE__) || defined(__linux__)
    const auto snap = signals::tui::derive(*a, *b, 8, 0);
    const auto it = std::find_if(snap.thread_load.begin(), snap.thread_load.end(), [](const auto& t) { return t.name == "signals-test"; });
    REQUIRE(it != snap.thread_load.end());
    CHECK(it->pct > 50);
    CHECK(snap.thread_load.front().pct >= snap.thread_load.back().pct);
#endif
}
