#pragma once

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace signals::test {

inline pipeline::PipelineDoc sig_doc(const std::string& text) {
    auto doc = pipeline::parse_sig(text);
    REQUIRE(doc.has_value());
    return *doc;
}

inline pipeline::RunResult run_doc(const pipeline::PipelineDoc& doc) {
    auto graph = pipeline::build_graph(doc, ".");
    REQUIRE(graph.has_value());
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    auto run = pipeline::run_graph(*graph, cache, {}, cancel);
    REQUIRE(run.has_value());
    return *run;
}

}  // namespace signals::test
