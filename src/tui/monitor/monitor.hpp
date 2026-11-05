#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

class ResourceMonitor {
public:
    explicit ResourceMonitor(std::function<void()> notify, std::chrono::milliseconds period = std::chrono::seconds{1});
    ~ResourceMonitor();
    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;

    ResourceSnapshot latest() const;
    std::vector<ResourceSnapshot> history() const;
    std::size_t samples() const;
    void frame_rendered();
    void set_queue_probe(std::function<std::size_t()> probe);

private:
    void loop();

    std::function<void()> notify_;
    std::chrono::milliseconds period_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    ResourceSnapshot latest_;
    std::deque<ResourceSnapshot> history_;
    std::size_t samples_ = 0;
    std::function<std::size_t()> probe_;
    std::atomic<std::size_t> frames_{0};
    std::thread worker_;
};

}  // namespace signals::tui
