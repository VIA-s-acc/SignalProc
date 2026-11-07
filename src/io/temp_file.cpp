#include "io/temp_file.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <random>
#include <system_error>

namespace signals::io {

std::optional<TempFile> create_temp_beside(const std::filesystem::path& target) {
    std::random_device entropy;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto tag = (static_cast<std::uint64_t>(entropy()) << 32) ^ entropy();
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(tag));
        auto path = target.parent_path() / ("." + target.filename().string() + "." + hex + ".partial");
        // "x" is exclusive creation: it fails on an existing file and on a planted symlink.
#ifdef _WIN32
        std::FILE* file = _wfopen(path.c_str(), L"wbx");
#else
        std::FILE* file = std::fopen(path.c_str(), "wbx");
#endif
        if (file) return TempFile{std::move(path), file};
        if (errno != EEXIST) return std::nullopt;
    }
    return std::nullopt;
}

void discard(TempFile& temp) {
    if (temp.file) std::fclose(temp.file);
    temp.file = nullptr;
    std::error_code ignored;
    std::filesystem::remove(temp.path, ignored);
}

core::Result<void> write_file_atomic(const std::filesystem::path& path, std::string_view bytes) {
    auto temp = create_temp_beside(path);
    if (!temp) return core::make_error(core::Errc::io_error, "cannot write file: " + path.string());
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), temp->file) == bytes.size();
    const bool flushed = std::fflush(temp->file) == 0;
    const bool closed = std::fclose(temp->file) == 0;
    temp->file = nullptr;
    std::error_code ec;
    if (written && flushed && closed) std::filesystem::rename(temp->path, path, ec);
    if (!written || !flushed || !closed || ec) {
        discard(*temp);
        return core::make_error(core::Errc::io_error, "cannot write file: " + path.string());
    }
    return {};
}

}  // namespace signals::io
