#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct Rename {
    enum class Kind { block, param, choice, port, field };
    Kind kind = Kind::param;
    std::string block, param, from, to, reason;
};

std::span<const Rename> v1_renames();
std::vector<std::string> apply_renames(PipelineDoc& doc, std::span<const Rename> renames);
std::string rename_expr_names(std::string_view expr, const std::map<std::string, std::string, std::less<>>& names);
std::optional<std::string> renamed(std::span<const Rename> renames, Rename::Kind kind, std::string_view block, std::string_view from);
std::string current_block_name(std::span<const Rename> renames, std::string_view v1_block);

}  // namespace signals::pipeline
