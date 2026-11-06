#include "core/thread_name.hpp"
#include "tui/monitor/monitor.hpp"

#include <algorithm>

namespace signals::tui {
namespace {

constexpr std::size_t kHistory = 300;

}  // namespace

ResourceMonitor::ResourceMonitor(std::function<void()> notify, std::chrono::milliseconds period)
    : notify_{std::move(notify)}, period_{period}, worker_{[this] { loop(); }} {}

ResourceMonitor::~ResourceMonitor() {
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

ResourceSnapshot ResourceMonitor::latest() const {
    std::lock_guard lock{mutex_};
    return latest_;
}

std::vector<ResourceSnapshot> ResourceMonitor::history() const {
    std::lock_guard lock{mutex_};
    return {history_.begin(), history_.end()};
}

std::size_t ResourceMonitor::samples() const {
    std::lock_guard lock{mutex_};
    return samples_;
}

void ResourceMonitor::frame_rendered() { ++frames_; }

void ResourceMonitor::set_queue_probe(std::function<std::size_t()> probe) {
    std::lock_guard lock{mutex_};
    probe_ = std::move(probe);
}

void ResourceMonitor::loop() {
    core::set_thread_name("signals-monitor");
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    auto prev = sample_process();
    for (;;) {
        {
            std::unique_lock lock{mutex_};
            if (wake_.wait_for(lock, period_, [this] { return stop_; })) return;
        }
        auto cur = sample_process();
        if (!cur || !prev) {
            prev = cur;
            continue;
        }
        const double dw = cur->wall_seconds - prev->wall_seconds;
        const double fps = dw > 0 ? static_cast<double>(frames_.exchange(0)) / dw : 0.0;
        auto snap = derive(*prev, *cur, cores, fps);
        {
            std::lock_guard lock{mutex_};
            snap.queued_jobs = probe_ ? probe_() : 0;
            latest_ = snap;
            history_.push_back(std::move(snap));
            while (history_.size() > kHistory) history_.pop_front();
            ++samples_;
        }
        prev = std::move(cur);
        if (notify_) notify_();
    }
}

}  // namespace signals::tui
