#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/error.hpp"

namespace signals::audio {

struct Clip {
    std::vector<std::span<const float>> channels;
    std::uint32_t rate = 0;
    std::shared_ptr<const void> owner;
    std::string tag;
    std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
};

class Sink {
public:
    using Pull = std::function<void(float* interleaved, std::size_t frames)>;
    virtual ~Sink() = default;
    virtual core::Result<void> start(std::uint32_t rate, std::uint32_t channels, Pull pull) = 0;
    virtual void stop() = 0;
};

core::Result<std::unique_ptr<Sink>> make_device_sink();

enum class PlayState { stopped, playing, finished, unavailable };

class Player {
public:
    explicit Player(core::Result<std::unique_ptr<Sink>> sink);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    core::Result<void> play(Clip clip, std::size_t from, std::optional<std::size_t> to = std::nullopt);
    void replace(Clip clip);
    void stop();
    void poll();
    PlayState state() const { return state_; }
    std::size_t position() const;
    std::string tag() const { return tag_; }
    std::optional<std::string> unavailable_reason() const { return reason_; }

private:
    struct Shared;
    void stop_sink();

    std::unique_ptr<Sink> sink_;
    std::shared_ptr<Shared> shared_;
    PlayState state_ = PlayState::stopped;
    std::optional<std::string> reason_;
    std::string tag_;
    std::uint32_t running_rate_ = 0, running_channels_ = 0;
    bool running_ = false;
};

}  // namespace signals::audio
