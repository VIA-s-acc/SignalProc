#include "commands/migrate_files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"
#include "pipeline/migrate.hpp"

namespace signals::cli {
namespace {

std::vector<std::filesystem::path> sig_files(std::span<const std::filesystem::path> paths) {
    std::vector<std::filesystem::path> out;
    for (const auto& p : paths) {
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) {
            out.push_back(p);
            continue;
        }
        std::vector<std::filesystem::path> found;
        for (auto it = std::filesystem::recursive_directory_iterator{p, ec}; !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec))
            if (it->is_regular_file() && it->path().extension() == ".sig" && !is_migration_backup(it->path())) found.push_back(it->path());
        std::ranges::sort(found);
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

std::filesystem::path backup_of(const std::filesystem::path& file) {
    auto backup = file;
    backup.replace_extension(".v1.sig");
    return backup;
}

FileReport migrate_one(const std::filesystem::path& file, bool write, bool backup) {
    FileReport r;
    r.file = file;
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        r.error = fmt::format(fmt::runtime(tr("pipeline.cli.cannot_read")), fmt::arg("path", file.string()));
        return r;
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    in.close();
    auto loaded = pipeline::load_sig(text);
    if (!loaded) {
        r.error = fmt::format("{}: {}", loaded.error().line, loaded.error().message);
        return r;
    }
    r.from = loaded->from_version;
    r.notes = std::move(loaded->notes);
    if (!write || r.from >= pipeline::kSigVersion) return r;
    if (backup)
        if (auto ok = io::write_file_atomic(backup_of(file), text); !ok) {
            r.error = ok.error().message;
            return r;
        }
    if (auto ok = io::write_file_atomic(file, pipeline::write_sig(loaded->doc)); !ok) {
        r.error = ok.error().message;
        return r;
    }
    r.written = true;
    return r;
}

}  // namespace

bool is_migration_backup(const std::filesystem::path& file) {
    const auto u = file.filename().u8string();
    return std::string_view{reinterpret_cast<const char*>(u.data()), u.size()}.ends_with(".v1.sig");
}

std::vector<FileReport> migrate_paths(std::span<const std::filesystem::path> paths, bool write, bool backup) {
    std::vector<FileReport> out;
    for (const auto& file : sig_files(paths)) out.push_back(migrate_one(file, write, backup));
    return out;
}

}  // namespace signals::cli
