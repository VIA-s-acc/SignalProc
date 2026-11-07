#pragma once

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

#include "core/error.hpp"

namespace signals::io {

struct TempFile {
    std::filesystem::path path;
    std::FILE* file = nullptr;
};

std::optional<TempFile> create_temp_beside(const std::filesystem::path& target);
void discard(TempFile& temp);
core::Result<void> write_file_atomic(const std::filesystem::path& path, std::string_view bytes);

}  // namespace signals::io
