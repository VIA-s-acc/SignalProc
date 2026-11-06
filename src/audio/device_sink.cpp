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
        config.pUserData = this;
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
        ma_device_uninit(&device_);
        initialised_ = false;
    }

private:
    static void callback(ma_device* device, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<DeviceSink*>(device->pUserData);
        self->pull_(static_cast<float*>(out), frames);
    }

    ma_device device_{};
    bool initialised_ = false;
    Pull pull_;
};

}  // namespace

core::Result<std::unique_ptr<Sink>> make_device_sink() { return std::unique_ptr<Sink>{std::make_unique<DeviceSink>()}; }

}  // namespace signals::audio
