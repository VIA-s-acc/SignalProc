#pragma once

#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/progress.hpp"
#include "pipeline/value.hpp"

namespace signals::pipeline {

struct ParamSpec {
    enum class Kind { integer, real, text, choice, path, range, samples, sigma, gains };
    std::string name;
    Kind kind = Kind::text;
    std::string fallback;
    std::vector<std::string> choices = {};
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
};

inline constexpr double kMaxSamples = 1 << 28;

struct PortSpec {
    std::string name;
    PortType type = PortType::signal;
    bool optional = false;
};

using Params = std::map<std::string, std::string, std::less<>>;

struct RunContext {
    std::filesystem::path base_dir;
    core::ProgressSink* progress = nullptr;
    std::vector<bool> owned = {};
};

// The executor marks an input as owned when the block is its only reader, nothing caches it and no probe looks at it;
// only then may the block move from it instead of copying.
template <class T>
T take(const RunContext& ctx, std::span<const Value> in, std::size_t i) {
    if (i < ctx.owned.size() && ctx.owned[i]) return std::move(const_cast<T&>(std::get<T>(in[i])));
    return std::get<T>(in[i]);
}

class ScaledProgress final : public core::ProgressSink {
public:
    ScaledProgress(core::ProgressSink* outer, double base, double span) : outer_{outer}, base_{base}, span_{span} {}
    void report(double fraction) override {
        if (outer_) outer_->report(base_ + span_ * fraction);
    }
    bool cancelled() const override { return outer_ && outer_->cancelled(); }

private:
    core::ProgressSink* outer_;
    double base_, span_;
};

inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

inline bool cancelled(const RunContext& ctx) { return ctx.progress && ctx.progress->cancelled(); }

using RunFn = std::function<core::Result<std::vector<Value>>(const RunContext&, std::span<const Value>, const Params&)>;

struct BlockType {
    std::string id;
    std::vector<PortSpec> inputs, outputs;
    std::vector<ParamSpec> params;
    RunFn run;
    bool side_effect = false;
};

const BlockType* find_block_type(std::string_view id);
std::span<const BlockType> block_types();
core::Result<Params> resolve_params(const BlockType& t, const Params& given);

double param_real(const Params& p, std::string_view name);
long long param_int(const Params& p, std::string_view name);
const std::string& param_text(const Params& p, std::string_view name);
core::Result<std::size_t> param_samples(const Params& p, std::string_view name, double rate);
std::pair<std::size_t, std::size_t> param_range(const Params& p, std::string_view name);
core::Result<std::size_t> checked_length(std::size_t n, std::size_t factor);

void register_basic_blocks(std::vector<BlockType>& out);
void register_dsp_blocks(std::vector<BlockType>& out);
void register_level_blocks(std::vector<BlockType>& out);
void register_source_blocks(std::vector<BlockType>& out);
void register_filter_blocks(std::vector<BlockType>& out);
void register_wavelet_blocks(std::vector<BlockType>& out);
void register_channel_blocks(std::vector<BlockType>& out);
void register_measure_blocks(std::vector<BlockType>& out);
void register_restore_blocks(std::vector<BlockType>& out);
void register_fx_blocks(std::vector<BlockType>& out);
void register_util_blocks(std::vector<BlockType>& out);
void register_design_blocks(std::vector<BlockType>& out);
void register_analysis_blocks(std::vector<BlockType>& out);

}  // namespace signals::pipeline
