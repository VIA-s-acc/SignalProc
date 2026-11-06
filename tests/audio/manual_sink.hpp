#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "audio/player.hpp"
#include "core/error.hpp"

namespace signals::test {

class ManualSink final : public audio::Sink {
public:
    core::Result<void> start(std::uint32_t rate, std::uint32_t channels, Pull pull) override {
        std::lock_guard lock{mutex_};
        rate_ = rate;
        channels_ = channels;
        pull_ = std::move(pull);
        running = true;
        ++starts;
        return {};
    }
    void stop() override {
        std::lock_guard lock{mutex_};
        running = false;
        ++stops;
    }
    std::optional<std::string> lost() override {
        std::lock_guard lock{mutex_};
        return std::exchange(lost_, std::nullopt);
    }
    void lose(std::string reason) {
        std::lock_guard lock{mutex_};
        lost_ = std::move(reason);
    }
    void pump(std::size_t frames) {
        std::lock_guard lock{mutex_};
        last.assign(frames * channels_, -99.0f);
        if (running && pull_) pull_(last.data(), frames);
    }
    std::uint32_t rate() const { return rate_; }
    std::uint32_t channels() const { return channels_; }

    std::vector<float> last;
    std::atomic<bool> running = false;
    int starts = 0, stops = 0;

private:
    std::mutex mutex_;
    std::optional<std::string> lost_;
    std::uint32_t rate_ = 0, channels_ = 0;
    Pull pull_;
};

class FailingSink final : public audio::Sink {
public:
    core::Result<void> start(std::uint32_t, std::uint32_t, Pull) override {
        return core::make_error(core::Errc::io_error, "no output device");
    }
    void stop() override {}
};

inline audio::Player manual_player(ManualSink*& raw) {
    auto sink = std::make_unique<ManualSink>();
    raw = sink.get();
    return audio::Player{std::unique_ptr<audio::Sink>{std::move(sink)}};
}

}  // namespace signals::test
