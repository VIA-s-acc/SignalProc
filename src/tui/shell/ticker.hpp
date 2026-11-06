#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace signals::tui {

class Ticker {
public:
    explicit Ticker(std::function<void()> notify, std::chrono::milliseconds period = std::chrono::milliseconds{33});
    ~Ticker();
    Ticker(const Ticker&) = delete;
    Ticker& operator=(const Ticker&) = delete;

    void want(std::string_view who, bool on);
    bool active() const;
    void begin_frame();

private:
    void loop();

    std::function<void()> notify_;
    std::chrono::milliseconds period_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::set<std::string, std::less<>> wanted_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace signals::tui
