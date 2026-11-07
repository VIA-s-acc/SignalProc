#include "pipeline/docs.hpp"

#include <cmath>

#include <fmt/format.h>

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

std::string blocks_markdown() {
    std::string out = "# " + tr("docs.blocks.title") + "\n\n" + tr("docs.blocks.generated") + "\n\n";
    out += "| " + tr("docs.blocks.col.block") + " | " + tr("docs.blocks.col.name") + " | " + tr("docs.blocks.col.inputs") + " | " +
           tr("docs.blocks.col.outputs") + " | " + tr("docs.blocks.col.params") + " |\n|---|---|---|---|---|\n";
    for (const auto& b : block_types())
        out += "| `" + b.id + "` | " + block_title(b) + " | " + ports(b.inputs) + " | " + ports(b.outputs) + " | " + params(b) + " |\n";
    return out;
}

std::string blocks_text() {
    std::string out;
    for (const auto& b : block_types()) out += fmt::format("{:<14} {}\n", b.id, block_title(b));
    return out;
}

}  // namespace signals::pipeline
