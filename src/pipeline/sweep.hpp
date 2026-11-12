#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "pipeline/sig.hpp"

namespace signals::pipeline {

struct SweepAxis {
    std::string node, param;
    std::vector<std::string> values;
};

enum class Metric { rms, peak, energy, mean, length };

struct Measure {
    std::string text;
    std::string node;
    std::string field = {};
    std::optional<Metric> metric = {};
};

struct SweepRow {
    std::string value;
    std::vector<std::optional<double>> results;
    std::string error = {};
};

struct SweepResult {
    SweepAxis axis;
    std::vector<Measure> measures;
    std::vector<SweepRow> rows;
};

using SweepProgress = std::function<void(std::size_t done, std::size_t total)>;

core::Result<SweepAxis> parse_vary(std::string_view text);
core::Result<Measure> parse_measure(std::string_view text);
core::Result<SweepResult> run_sweep(const PipelineDoc& doc, const std::filesystem::path& base_dir, SweepAxis axis, std::vector<Measure> measures,
                                    const core::CancelToken& cancel, const SweepProgress& progress = {});
std::optional<double> numeric(std::string_view value);

}  // namespace signals::pipeline
