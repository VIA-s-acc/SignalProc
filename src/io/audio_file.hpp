#pragma once

#include <filesystem>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "io/wav.hpp"

namespace signals::io {

core::Result<WavInfo> probe_audio(const std::filesystem::path& path);
core::Result<WavFile> read_audio(const std::filesystem::path& path, core::ProgressSink* progress = nullptr);

}  // namespace signals::io
