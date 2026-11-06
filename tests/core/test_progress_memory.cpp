#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <utility>

#include "core/design/daubechies.hpp"
#include "core/design/families.hpp"
#include "core/memory.hpp"

using namespace signals::core;

TEST_CASE("tracked bytes add and release", "[memory]") {
    const auto idx = static_cast<std::size_t>(MemoryCategory::display);
    const auto before = tracked_memory()[idx];
    {
        TrackedBytes a{MemoryCategory::display, 1000};
        CHECK(tracked_memory()[idx] == before + 1000);
        TrackedBytes b = std::move(a);
        CHECK(tracked_memory()[idx] == before + 1000);
        b.reset(1500);
        CHECK(tracked_memory()[idx] == before + 1500);
        TrackedBytes c;
        c = std::move(b);
        CHECK(tracked_memory()[idx] == before + 1500);
    }
    CHECK(tracked_memory()[idx] == before);
}

TEST_CASE("a repeated wavelet is served from the cache", "[families]") {
    using clock = std::chrono::steady_clock;
    REQUIRE(wavelet("sym17"));
    const auto t0 = clock::now();
    auto fresh = symlet(17);
    const auto t1 = clock::now();
    auto cached = wavelet("sym17");
    const auto t2 = clock::now();
    REQUIRE(fresh);
    REQUIRE(cached);
    CHECK(fresh->h0.coeffs == cached->h0.coeffs);
    CHECK((t2 - t1) * 10 < (t1 - t0));
    CHECK_FALSE(wavelet("sym99"));
}
