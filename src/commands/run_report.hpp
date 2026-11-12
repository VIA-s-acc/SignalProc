#pragma once

#include <filesystem>

#include "core/error.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace signals::cli {

core::Result<void> write_run_report(const std::filesystem::path& report, const pipeline::PipelineDoc& doc, const pipeline::Graph& graph,
                                    const pipeline::RunResult& result, const std::filesystem::path& sig_file);

}  // namespace signals::cli
