#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "learn/task.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/sig.hpp"

namespace signals::learn {

std::string probe_name(std::string_view endpoint);
void add_measure_probes(pipeline::PipelineDoc& doc, std::span<const Measure> measures, bool reference = false);

struct MeasureValue {
    std::vector<double> values = {};
    double scalar() const;
};

core::Result<MeasureValue> compute_measure(const Measure& m, const pipeline::PipelineDoc& doc, const pipeline::RunResult& run,
                                           const pipeline::RunResult* reference = nullptr);

}  // namespace signals::learn
