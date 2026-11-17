#pragma once

#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct Scope {
    std::map<std::string, std::string, std::less<>> values;
};

struct FlattenOptions {};

struct LetValue {
    std::string name, text;
    std::optional<double> number;
    std::string expanded;
};

std::expected<Scope, SigError> resolve_lets(std::span<const LetDecl> lets);
core::Result<std::string> substitute(std::string_view value, const Scope& scope, ParamSpec::Kind kind);
std::vector<std::string> variables_in(std::string_view value);
std::vector<LetValue> let_values(std::span<const LetDecl> lets);
std::expected<PipelineDoc, SigError> flatten(const PipelineDoc& doc, const FlattenOptions& options = {});

}  // namespace signals::pipeline
