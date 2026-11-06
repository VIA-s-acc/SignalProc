#include "audio/player.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

namespace signals::audio {

struct Player::Shared {
    std::mutex mutex;
    Clip clip;
    std::size_t end = 0;
    bool active = false;
    std::atomic<std::size_t> position{0};
    std::atomic<bool> ended{false};

    void pull(float* out, std::size_t frames, std::uint32_t out_channels) {
        std::unique_lock lock{mutex, std::try_to_lock};
        const std::size_t samples = frames * out_channels;
        if (!lock.owns_lock() || !active) {
            std::fill(out, out + samples, 0.0f);
            return;
        }
        const auto channels = std::min<std::size_t>(clip.channels.size(), out_channels);
        std::size_t pos = position.load(std::memory_order_relaxed);
        std::size_t f = 0;
        for (; f < frames && pos < end; ++f, ++pos)
            for (std::size_t c = 0; c < out_channels; ++c) out[f * out_channels + c] = c < channels ? clip.channels[c][pos] : 0.0f;
        std::fill(out + f * out_channels, out + samples, 0.0f);
        position.store(pos, std::memory_order_relaxed);
        if (pos >= end) ended.store(true, std::memory_order_relaxed);
    }
};

Player::Player(core::Result<std::unique_ptr<Sink>> sink) : shared_{std::make_shared<Shared>()} {
    if (sink && *sink) {
        sink_ = std::move(*sink);
    } else {
        reason_ = sink ? std::string{"no audio sink"} : sink.error().message;
        state_ = PlayState::unavailable;
    }
}

Player::~Player() { stop_sink(); }

void Player::stop_sink() {
    if (sink_ && running_) sink_->stop();
    running_ = false;
}

core::Result<void> Player::play(Clip clip, std::size_t from, std::optional<std::size_t> to) {
    if (state_ == PlayState::unavailable) return core::make_error(core::Errc::io_error, reason_.value_or("audio unavailable"));
    const auto frames = clip.frames();
    if (frames == 0 || clip.rate == 0) return core::make_error(core::Errc::invalid_argument, "play: empty clip");
    if (from >= frames) return core::make_error(core::Errc::invalid_argument, "play: start is past the end of the clip");
    const auto rate = clip.rate;
    const auto channels = static_cast<std::uint32_t>(std::min<std::size_t>(clip.channels.size(), 2));
    const auto end = std::clamp(to.value_or(frames), from + 1, frames);
    tag_ = clip.tag;
    {
        std::lock_guard lock{shared_->mutex};
        shared_->clip = std::move(clip);
        shared_->end = end;
        shared_->position.store(from);
        shared_->ended.store(false);
        shared_->active = true;
    }
    if (!running_ || rate != running_rate_ || channels != running_channels_) {
        stop_sink();
        auto shared = shared_;
        auto started = sink_->start(rate, channels, [shared, channels](float* out, std::size_t n) { shared->pull(out, n, channels); });
        if (!started) {
            {
                std::lock_guard lock{shared_->mutex};
                shared_->active = false;
            }
            reason_ = started.error().message;
            state_ = PlayState::unavailable;
            return started;
        }
        running_ = true;
        running_rate_ = rate;
        running_channels_ = channels;
    }
    state_ = PlayState::playing;
    return {};
}

void Player::replace(Clip clip) {
    if (state_ != PlayState::playing) return;
    const auto channels = static_cast<std::uint32_t>(std::min<std::size_t>(clip.channels.size(), 2));
    if (clip.frames() == 0 || clip.rate != running_rate_ || channels != running_channels_) {
        const auto pos = position();
        (void)play(std::move(clip), pos);
        return;
    }
    tag_ = clip.tag;
    std::lock_guard lock{shared_->mutex};
    const auto frames = clip.frames();
    shared_->clip = std::move(clip);
    shared_->end = frames;
    if (shared_->position.load() > frames) shared_->position.store(frames);
    shared_->ended.store(shared_->position.load() >= frames);
}

void Player::stop() {
    {
        std::lock_guard lock{shared_->mutex};
        shared_->active = false;
    }
    stop_sink();
    if (state_ != PlayState::unavailable) state_ = PlayState::stopped;
}

void Player::poll() {
    if (running_ && sink_) {
        if (auto why = sink_->lost()) {
            const bool was_playing = state_ == PlayState::playing;
            stop();
            if (was_playing) interruption_ = std::move(why);
            return;
        }
    }
    if (state_ != PlayState::playing || !shared_->ended.load()) return;
    {
        std::lock_guard lock{shared_->mutex};
        shared_->active = false;
    }
    stop_sink();
    state_ = PlayState::finished;
}

std::size_t Player::position() const { return shared_->position.load(); }

}  // namespace signals::audio
