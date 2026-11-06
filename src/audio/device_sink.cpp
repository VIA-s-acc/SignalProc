#include <atomic>

#include <miniaudio.h>

#include "audio/player.hpp"

namespace signals::audio {
namespace {

class DeviceSink final : public Sink {
public:
    ~DeviceSink() override { stop(); }

    core::Result<void> start(std::uint32_t rate, std::uint32_t channels, Pull pull) override {
        stop();
        pull_ = std::move(pull);
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = channels;
        config.sampleRate = rate;
        config.dataCallback = &DeviceSink::callback;
        config.notificationCallback = &DeviceSink::notify;
        config.pUserData = this;
        lost_.store(Lost::none);
        stopping_.store(false);
        if (const auto r = ma_device_init(nullptr, &config, &device_); r != MA_SUCCESS)
            return core::make_error(core::Errc::io_error, std::string{"audio device: "} + ma_result_description(r));
        initialised_ = true;
        if (const auto r = ma_device_start(&device_); r != MA_SUCCESS) {
            stop();
            return core::make_error(core::Errc::io_error, std::string{"audio device: "} + ma_result_description(r));
        }
        return {};
    }

    void stop() override {
        if (!initialised_) return;
        stopping_.store(true);
        ma_device_uninit(&device_);
        initialised_ = false;
    }

    std::optional<std::string> lost() override {
        switch (lost_.exchange(Lost::none)) {
        case Lost::stopped: return "audio device stopped";
        case Lost::interrupted: return "audio interrupted by the system";
        case Lost::none: break;
        }
        return std::nullopt;
    }

private:
    static void callback(ma_device* device, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<DeviceSink*>(device->pUserData);
        self->pull_(static_cast<float*>(out), frames);
    }

    static void notify(const ma_device_notification* n) {
        auto* self = static_cast<DeviceSink*>(n->pDevice->pUserData);
        if (self->stopping_.load()) return;
        if (n->type == ma_device_notification_type_stopped) self->lost_.store(Lost::stopped);
        else if (n->type == ma_device_notification_type_interruption_began) self->lost_.store(Lost::interrupted);
    }

    enum class Lost { none, stopped, interrupted };

    ma_device device_{};
    std::atomic<Lost> lost_{Lost::none};
    std::atomic<bool> stopping_{false};
    bool initialised_ = false;
    Pull pull_;
};

}  // namespace

core::Result<std::unique_ptr<Sink>> make_device_sink() { return std::unique_ptr<Sink>{std::make_unique<DeviceSink>()}; }

}  // namespace signals::audio
