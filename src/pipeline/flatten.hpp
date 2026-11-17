#pragma once

#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "core/error.hpp"
#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct Scope {
    std::map<std::string, std::string, std::less<>> values;
};

struct FlattenOptions {};

std::expected<Scope, SigError> resolve_lets(std::span<const LetDecl> lets);
core::Result<std::string> substitute(std::string_view value, const Scope& scope, ParamSpec::Kind kind);
std::expected<PipelineDoc, SigError> flatten(const PipelineDoc& doc, const FlattenOptions& options = {});

}  // namespace signals::pipeline
