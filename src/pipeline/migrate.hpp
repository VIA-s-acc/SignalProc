#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct Loaded {
    PipelineDoc doc;
    int from_version = kSigVersion;
    std::vector<std::string> notes;
};

struct LoadOptions {
    std::filesystem::path base_dir = {};
    std::vector<std::filesystem::path> using_chain = {};
};

struct MigrationStep {
    int from = 1, to = 2;
    std::function<std::vector<std::string>(PipelineDoc&)> apply;
};

std::span<const MigrationStep> migration_steps();
std::vector<std::string> migrate_doc(PipelineDoc& doc);
std::expected<Loaded, SigError> load_sig(std::string_view text, const LoadOptions& options = {});
std::vector<std::filesystem::path> used_files(std::string_view text, const std::filesystem::path& base_dir);

}  // namespace signals::pipeline
