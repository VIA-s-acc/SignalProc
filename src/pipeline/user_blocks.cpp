#include "pipeline/user_blocks.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "pipeline/migrate.hpp"

namespace signals::pipeline {

std::optional<std::filesystem::path> user_blocks_dir(const io::EnvLookup& env) {
    const auto dir = io::config_dir(env);
    if (!dir) return std::nullopt;
    return *dir / "blocks";
}

namespace {

std::vector<std::filesystem::path> sig_files(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& e : std::filesystem::directory_iterator{dir, ec})
        if (e.is_regular_file(ec) && e.path().extension() == ".sig") out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

}  // namespace

std::vector<std::filesystem::path> user_block_files(const io::EnvLookup& env) {
    const auto dir = user_blocks_dir(env);
    return dir ? sig_files(*dir) : std::vector<std::filesystem::path>{};
}

std::vector<BlockDef> load_user_blocks(const std::filesystem::path& dir) {
    std::vector<BlockDef> out;
    for (const auto& file : sig_files(dir)) {
        std::ifstream in{file, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        auto loaded = load_sig(text, {dir});
        if (!loaded) continue;
        for (auto* list : {&loaded->doc.imported, &loaded->doc.blocks})
            for (auto& b : *list) {
                if (b.origin.empty()) b.origin = file;
                if (std::ranges::none_of(out, [&](const BlockDef& d) { return d.name == b.name; })) out.push_back(std::move(b));
            }
    }
    return out;
}

std::optional<LibraryBlock> find_user_block(std::string_view name, const io::EnvLookup& env) {
    for (const auto& file : user_block_files(env)) {
        std::ifstream in{file, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        auto loaded = load_sig(text, {file.parent_path()});
        if (!loaded) continue;
        if (std::ranges::any_of(loaded->doc.blocks, [&](const BlockDef& b) { return b.name == name; })) return LibraryBlock{std::move(loaded->doc), file};
    }
    return std::nullopt;
}

}  // namespace signals::pipeline
