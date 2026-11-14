#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "io/npy.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace signals::python {

enum class ScriptKind { script, notebook };

struct ScriptOptions {
    ScriptKind kind = ScriptKind::script;
    std::filesystem::path out;
    std::filesystem::path sig;
    std::string source;
};

struct CheckRow {
    std::string what;
    std::string origin;
    double tol = 1e-12;
};

struct Script {
    std::string text;
    io::NpzEntries data;
    std::vector<CheckRow> checks;
};

core::Result<Script> build_script(const pipeline::PipelineDoc& doc, const pipeline::Graph& graph, const ScriptOptions& options, const core::CancelToken& cancel);

}  // namespace signals::python
