#pragma once

#include <atomic>
#include <cstddef>

namespace signals::core {

class ProgressSink {
public:
    virtual ~ProgressSink() = default;
    virtual void report(double fraction) = 0;
    virtual bool cancelled() const = 0;
};

class CancelToken {
public:
    void cancel() { flag_.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return flag_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> flag_{false};
};

inline bool stop_requested(const ProgressSink* progress, std::size_t i) { return progress && (i & 0xFFFF) == 0 && progress->cancelled(); }

}  // namespace signals::core
