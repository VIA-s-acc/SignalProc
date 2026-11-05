#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include "tui/jobs/jobs.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

namespace {

JobView wait_finished(const JobRunner& r, std::uint64_t id) {
    for (int k = 0; k < 2000; ++k) {
        for (const auto& v : r.snapshot())
            if (v.id == id && v.finished) return v;
        std::this_thread::sleep_for(1ms);
    }
    FAIL("job did not finish");
    return {};
}

}  // namespace

TEST_CASE("stages move through their states", "[tui][jobs]") {
    JobRunner r{[] {}, 0ms, true};
    auto id = r.submit("load", {"read", "stats"}, [](JobContext& c) {
        c.begin(0);
        c.report(0.5);
        c.finish(100, "ok");
        c.begin(1);
        c.finish();
    });
    auto v = r.snapshot().back();
    CHECK(v.id == id);
    CHECK(v.title == "load");
    CHECK(v.finished);
    CHECK_FALSE(v.cancelled);
    CHECK(v.stages[0].state == StageState::done);
    CHECK(v.stages[0].bytes == 100);
    CHECK(v.stages[0].detail == "ok");
    CHECK(v.stages[0].fraction == 1.0);
    CHECK(v.stages[1].state == StageState::done);
    CHECK(v.stages[0].seconds >= 0);
}

TEST_CASE("unstarted stages are skipped when a job ends early", "[tui][jobs]") {
    JobRunner r{[] {}, 0ms, true};
    r.submit("x", {"a", "b"}, [](JobContext& c) { c.begin(0); c.finish(); });
    CHECK(r.snapshot().back().stages[1].state == StageState::skipped);
}

TEST_CASE("notifications are throttled but the end always arrives", "[tui][jobs]") {
    std::atomic<int> count{0};
    JobRunner r{[&] { ++count; }, 50ms};
    const auto t0 = std::chrono::steady_clock::now();
    auto id = r.submit("busy", {"spin"}, [](JobContext& c) {
        c.begin(0);
        const auto start = std::chrono::steady_clock::now();
        for (int k = 0; k < 10'000; ++k) {
            c.report(k / 10'000.0);
            while (std::chrono::steady_clock::now() - start < std::chrono::microseconds{10 * k}) {}
        }
        c.finish();
    });
    auto v = wait_finished(r, id);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(v.finished);
    CHECK(count.load() >= 1);
    CHECK(count.load() <= elapsed / 50 + 2);
}

TEST_CASE("a cancelled job skips its remaining stages", "[tui][jobs]") {
    std::atomic<bool> started{false};
    JobRunner r{[] {}};
    auto id = r.submit("spin", {"loop", "after"}, [&](JobContext& c) {
        c.begin(0);
        started = true;
        while (!c.cancelled()) std::this_thread::sleep_for(1ms);
    });
    while (!started) std::this_thread::sleep_for(1ms);
    r.cancel(id);
    auto v = wait_finished(r, id);
    CHECK(v.cancelled);
    CHECK_FALSE(v.error);
    CHECK(v.stages[1].state == StageState::skipped);
}

TEST_CASE("queued jobs can be cancelled before they start", "[tui][jobs]") {
    std::atomic<bool> release{false}, started{false};
    JobRunner r{[] {}};
    r.submit("blocker", {"wait"}, [&](JobContext&) {
        started = true;
        while (!release) std::this_thread::sleep_for(1ms);
    });
    while (!started) std::this_thread::sleep_for(1ms);
    auto queued = r.submit("later", {"never"}, [](JobContext&) { FAIL("must not run"); });
    CHECK(r.queued() == 1);
    r.cancel(queued);
    CHECK(r.queued() == 0);
    release = true;
    auto v = wait_finished(r, queued);
    CHECK(v.cancelled);
    CHECK(v.stages[0].state == StageState::skipped);
}

TEST_CASE("a throwing job reports the error and the runner goes on", "[tui][jobs]") {
    JobRunner r{[] {}};
    auto bad = r.submit("bad", {"boom"}, [](JobContext& c) {
        c.begin(0);
        throw std::runtime_error("boom");
    });
    auto good = r.submit("good", {"ok"}, [](JobContext& c) { c.begin(0); c.finish(); });
    auto b = wait_finished(r, bad);
    REQUIRE(b.error);
    CHECK(*b.error == "boom");
    CHECK(b.stages[0].state == StageState::failed);
    CHECK(wait_finished(r, good).stages[0].state == StageState::done);
}

TEST_CASE("fail marks the stage and the job", "[tui][jobs]") {
    JobRunner r{[] {}, 0ms, true};
    r.submit("x", {"a"}, [](JobContext& c) { c.begin(0); c.fail("no such file"); });
    auto v = r.snapshot().back();
    CHECK(v.stages[0].state == StageState::failed);
    REQUIRE(v.error);
    CHECK(*v.error == "no such file");
}

TEST_CASE("destroying the runner mid-job is prompt and silent", "[tui][jobs]") {
    auto destroyed = std::make_shared<std::atomic<bool>>(false);
    auto late = std::make_shared<std::atomic<int>>(0);
    std::chrono::milliseconds took{};
    {
        auto r = std::make_unique<JobRunner>([destroyed, late] { if (*destroyed) ++*late; }, 0ms);
        r->submit("spin", {"loop"}, [](JobContext& c) {
            c.begin(0);
            while (!c.cancelled()) c.report(0.5);
        });
        std::this_thread::sleep_for(20ms);
        const auto t0 = std::chrono::steady_clock::now();
        r.reset();
        took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        *destroyed = true;
    }
    std::this_thread::sleep_for(20ms);
    CHECK(took < 200ms);
    CHECK(late->load() == 0);
}

TEST_CASE("history keeps the last twenty jobs", "[tui][jobs]") {
    JobRunner r{[] {}, 0ms, true};
    for (int k = 0; k < 25; ++k) r.submit("j" + std::to_string(k), {"s"}, [](JobContext& c) { c.begin(0); c.finish(); });
    auto s = r.snapshot();
    CHECK(s.size() == 20);
    CHECK(s.front().title == "j5");
    CHECK(s.back().title == "j24");
}
