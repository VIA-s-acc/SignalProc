#include <algorithm>
#include <chrono>

#include <sys/resource.h>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

namespace detail {

double process_cpu_seconds() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    auto sec = [](timeval t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) * 1e-6; };
    return sec(ru.ru_utime) + sec(ru.ru_stime);
}

double wall_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace detail

ResourceSnapshot derive(const ProcessSample& prev, const ProcessSample& cur, unsigned cores, double fps) {
    ResourceSnapshot s;
    const double dw = cur.wall_seconds - prev.wall_seconds;
    if (dw > 0) s.cpu_one_core_pct = std::max(0.0, (cur.cpu_seconds - prev.cpu_seconds) / dw * 100);
    s.cpu_all_cores_pct = s.cpu_one_core_pct / std::max(1u, cores);
    s.rss = cur.rss_bytes;
    s.peak_rss = cur.peak_rss_bytes;
    s.threads = cur.threads;
    s.fps = fps;
    const std::size_t n = std::min({cur.core_busy_ticks.size(), cur.core_total_ticks.size(), prev.core_busy_ticks.size(), prev.core_total_ticks.size()});
    s.core_load_pct.resize(cur.core_busy_ticks.size(), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const auto busy = cur.core_busy_ticks[i] - prev.core_busy_ticks[i], total = cur.core_total_ticks[i] - prev.core_total_ticks[i];
        if (total > 0) s.core_load_pct[i] = 100.0 * static_cast<double>(busy) / static_cast<double>(total);
    }
    s.buffers = core::tracked_memory();
    return s;
}

}  // namespace signals::tui
