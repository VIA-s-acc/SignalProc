#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "core/audio_buffer.hpp"
#include "core/error.hpp"
#include "core/progress.hpp"

namespace signals::io {

enum class SampleFormat { pcm_u8, pcm_s16, pcm_s24, pcm_s32, float32, float64, alaw, mulaw };

std::string_view to_string(SampleFormat format);

struct WavInfo {
    SampleFormat format = SampleFormat::pcm_s16;
    unsigned channels = 0;
    unsigned sample_rate = 0;
    std::uint64_t frames = 0;
    bool extensible = false;
};

struct WavFile {
    WavInfo info;
    core::AudioBuffer audio;
};

core::Result<WavInfo> probe_wav(const std::filesystem::path& path);
core::Result<WavFile> read_wav(const std::filesystem::path& path);
core::Result<WavFile> read_wav(const std::filesystem::path& path, core::ProgressSink* progress);

struct WavWriteOptions {
    SampleFormat format = SampleFormat::pcm_s24;
    bool dither = true;
    std::uint64_t dither_seed = 0x5eed;
};

struct WavWriteReport {
    std::size_t clipped_samples = 0;
};

core::Result<WavWriteReport> write_wav(const std::filesystem::path& path, const core::AudioBuffer& audio,
                                       const WavWriteOptions& options);

}  // namespace signals::io
