#pragma once

#include <filesystem>
#include <functional>
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
    enum class Kind { integer, real, text, choice, path, range, samples };
    std::string name;
    Kind kind = Kind::text;
    std::string fallback;
    std::vector<std::string> choices = {};
};

struct PortSpec {
    std::string name;
    PortType type = PortType::signal;
    bool optional = false;
};

using Params = std::map<std::string, std::string, std::less<>>;

struct RunContext {
    std::filesystem::path base_dir;
    core::ProgressSink* progress = nullptr;
};

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

void register_basic_blocks(std::vector<BlockType>& out);
void register_dsp_blocks(std::vector<BlockType>& out);

}  // namespace signals::pipeline
