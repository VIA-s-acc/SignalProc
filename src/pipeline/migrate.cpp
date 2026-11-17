#include "pipeline/migrate.hpp"

#include <array>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

std::vector<std::string> v1_to_v2(PipelineDoc& doc) {
    std::vector<std::string> notes;
    for (auto& node : doc.nodes)
        for (auto& [param, value] : node.params) {
            if (value.find('$') == std::string::npos) continue;
            std::string escaped;
            for (const char c : value) escaped += c == '$' ? std::string{"$$"} : std::string(1, c);
            value = std::move(escaped);
            notes.push_back(fmt::format(fmt::runtime(tr("pipeline.migrate.dollar")), fmt::arg("node", node.id), fmt::arg("param", param)));
        }
    return notes;
}

const std::array<MigrationStep, 1> kSteps{MigrationStep{1, 2, v1_to_v2}};

}  // namespace

std::span<const MigrationStep> migration_steps() { return kSteps; }

std::vector<std::string> migrate_doc(PipelineDoc& doc) {
    std::vector<std::string> notes;
    for (const auto& step : kSteps) {
        if (doc.version != step.from) continue;
        auto more = step.apply(doc);
        notes.insert(notes.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        doc.version = step.to;
    }
    return notes;
}

std::expected<Loaded, SigError> load_sig(std::string_view text, const LoadOptions&) {
    auto doc = parse_sig(text);
    if (!doc) return std::unexpected{doc.error()};
    Loaded out{std::move(*doc), 0, {}};
    out.from_version = out.doc.version;
    out.notes = migrate_doc(out.doc);
    return out;
}

}  // namespace signals::pipeline
