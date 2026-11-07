#include "pipeline/docs.hpp"

#include <cmath>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/value.hpp"

namespace signals::pipeline {
namespace {

std::string block_title(const BlockType& b) {
    const auto key = "pipeline.block." + b.id;
    const auto name = tr(key);
    return name == key ? b.id : name;
}

std::string ports(const std::vector<PortSpec>& list) {
    if (list.empty()) return "—";
    std::string out;
    for (const auto& p : list) {
        if (!out.empty()) out += "; ";
        out += "`" + p.name + "`: " + std::string{to_string(p.type)};
        if (p.optional) out += " (" + tr("docs.blocks.optional") + ")";
    }
    return out;
}

std::string params(const BlockType& b) {
    if (b.params.empty()) return "—";
    std::string out;
    for (const auto& p : b.params) {
        if (!out.empty()) out += "; ";
        out += "`" + p.name + "` = " + (p.fallback.empty() ? std::string{"—"} : p.fallback);
        if (!p.choices.empty()) {
            std::string list;
            for (const auto& c : p.choices) list += (list.empty() ? "" : ", ") + c;
            out += " (" + list + ")";
        } else if (std::isfinite(p.min) && std::isfinite(p.max)) {
            out += fmt::format(" ({:g}…{:g})", p.min, p.max);
        }
    }
    return out;
}

}  // namespace

std::string markdown_cell(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '|') out += "\\|";
        else out += c == '\n' ? ' ' : c;
    }
    return out;
}

std::string blocks_markdown() {
    std::string out = "# " + tr("docs.blocks.title") + "\n\n" + tr("docs.blocks.generated") + "\n\n";
    out += "| " + tr("docs.blocks.col.block") + " | " + tr("docs.blocks.col.name") + " | " + tr("docs.blocks.col.about") + " | " + tr("docs.blocks.col.inputs") +
           " | " + tr("docs.blocks.col.outputs") + " | " + tr("docs.blocks.col.params") + " |\n|---|---|---|---|---|---|\n";
    for (const auto& b : block_types())
        out += "| `" + markdown_cell(b.id) + "` | " + markdown_cell(block_title(b)) + " | " + markdown_cell(tr_opt("pipeline.about." + b.id).value_or("—")) + " | " +
               markdown_cell(ports(b.inputs)) + " | " + markdown_cell(ports(b.outputs)) + " | " + markdown_cell(params(b)) + " |\n";
    out += "\n## " + tr("docs.blocks.params_title") + "\n";
    for (const auto& b : block_types()) {
        if (b.params.empty()) continue;
        out += "\n### `" + b.id + "` — " + block_title(b) + "\n\n";
        for (const auto& p : b.params) {
            out += "- `" + p.name + "` — " + tr_opt("pipeline.param." + b.id + "." + p.name).value_or("—") + "\n";
            for (const auto& v : p.choices) out += "  - `" + v + "` — " + tr_opt("pipeline.param." + b.id + "." + p.name + "." + v).value_or("—") + "\n";
        }
    }
    return out;
}

std::string blocks_json() {
    auto ports_json = [](const std::vector<PortSpec>& list) {
        auto out = nlohmann::json::array();
        for (const auto& p : list) out.push_back({{"name", p.name}, {"type", std::string{to_string(p.type)}}, {"optional", p.optional}});
        return out;
    };
    auto out = nlohmann::json::array();
    for (const auto& b : block_types()) {
        auto params_json = nlohmann::json::array();
        for (const auto& p : b.params) {
            nlohmann::json param{{"name", p.name}, {"default", p.fallback}, {"about", tr_opt("pipeline.param." + b.id + "." + p.name).value_or("")}};
            if (std::isfinite(p.min)) param["min"] = p.min;
            if (std::isfinite(p.max)) param["max"] = p.max;
            auto values = nlohmann::json::object();
            for (const auto& v : p.choices) values[v] = tr_opt("pipeline.param." + b.id + "." + p.name + "." + v).value_or("");
            param["choices"] = p.choices;
            param["values"] = values;
            params_json.push_back(std::move(param));
        }
        out.push_back({{"id", b.id}, {"title", block_title(b)}, {"about", tr_opt("pipeline.about." + b.id).value_or("")}, {"inputs", ports_json(b.inputs)},
                       {"outputs", ports_json(b.outputs)}, {"params", std::move(params_json)}});
    }
    return out.dump(1) + "\n";
}

std::string blocks_text() {
    std::string out;
    for (const auto& b : block_types()) out += fmt::format("{:<18} {} — {}\n", b.id, block_title(b), tr_opt("pipeline.about." + b.id).value_or(""));
    return out;
}

}  // namespace signals::pipeline
