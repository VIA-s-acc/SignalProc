#include "core/thread_name.hpp"
#include "tui/shell/ticker.hpp"

#include <utility>

namespace signals::tui {

Ticker::Ticker(std::function<void()> notify, std::chrono::milliseconds period)
    : notify_{std::move(notify)}, period_{period}, thread_{[this] { loop(); }} {}

Ticker::~Ticker() {
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Ticker::want(std::string_view who, bool on) {
    {
        std::lock_guard lock{mutex_};
        const auto it = wanted_.find(who);
        if (on == (it != wanted_.end())) return;
        if (on) wanted_.emplace(who);
        else wanted_.erase(it);
    }
    wake_.notify_all();
}

bool Ticker::active() const {
    std::lock_guard lock{mutex_};
    return !wanted_.empty();
}

void Ticker::begin_frame() {
    std::lock_guard lock{mutex_};
    wanted_.clear();
}

void Ticker::loop() {
    core::set_thread_name("signals-ticker");
    std::unique_lock lock{mutex_};
    while (!stop_) {
        if (wanted_.empty()) {
            wake_.wait(lock, [this] { return stop_ || !wanted_.empty(); });
            continue;
        }
        if (wake_.wait_for(lock, period_, [this] { return stop_; })) break;
        if (wanted_.empty()) continue;
        lock.unlock();
        notify_();
        lock.lock();
    }
}

}  // namespace signals::tui
