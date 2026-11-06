#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/error.hpp"
#include "core/memory.hpp"

namespace signals::tui {

struct ProcessSample {
    double wall_seconds = 0;
    double cpu_seconds = 0;
    std::size_t rss_bytes = 0;
    std::size_t peak_rss_bytes = 0;
    unsigned threads = 0;
    std::vector<std::uint64_t> core_busy_ticks = {};
    std::vector<std::uint64_t> core_total_ticks = {};
};

core::Result<ProcessSample> sample_process();
std::uint64_t physical_memory_bytes();

struct ResourceSnapshot {
    double cpu_one_core_pct = 0;
    double cpu_all_cores_pct = 0;
    std::size_t rss = 0, peak_rss = 0;
    unsigned threads = 0;
    double fps = 0;
    std::vector<double> core_load_pct = {};
    std::array<std::size_t, core::kMemoryCategories> buffers{};
    std::size_t queued_jobs = 0;
};

ResourceSnapshot derive(const ProcessSample& prev, const ProcessSample& cur, unsigned cores, double fps);

namespace detail {
double process_cpu_seconds();
double wall_seconds();
}  // namespace detail

}  // namespace signals::tui
