#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/audio_buffer.hpp"
#include "core/error.hpp"
#include "io/npy.hpp"
#include "io/wav.hpp"

namespace signals::io {

struct ArrayAudioOptions {
    std::optional<double> rate;
    std::string key;
};

NpzEntries signal_entries(const core::AudioBuffer& a);
core::Result<core::AudioBuffer> audio_from_array(const NpArray& a, double rate);
core::Result<WavFile> read_array_audio(const std::filesystem::path& path, const ArrayAudioOptions& options);
core::Result<WavInfo> probe_array_audio(const std::filesystem::path& path, const std::string& key = "");
bool is_array_audio(const std::filesystem::path& path);

}  // namespace signals::io
