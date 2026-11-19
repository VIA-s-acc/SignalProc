#include "pipeline/migrate.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iterator>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/renames.hpp"

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
    auto renames = apply_renames(doc, v1_renames());
    notes.insert(notes.end(), std::make_move_iterator(renames.begin()), std::make_move_iterator(renames.end()));
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

std::expected<Loaded, SigError> load_sig(std::string_view text, const LoadOptions& options) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    auto doc = parse_sig(text);
    if (!doc) return std::unexpected{doc.error()};
    Loaded out{std::move(*doc), 0, {}};
    out.from_version = out.doc.version;
    out.notes = migrate_doc(out.doc);
    const auto say = [](const char* key, auto&&... args) { return fmt::format(fmt::runtime(tr(key)), std::forward<decltype(args)>(args)...); };
    const auto shown = [](const std::filesystem::path& p) {
        const auto u = p.filename().u8string();
        return std::string{u.begin(), u.end()};
    };
    for (const auto& use : out.doc.uses) {
        auto path = utf8_path(use.path);
        if (path.is_relative()) path = options.base_dir / path;
        path = std::filesystem::absolute(path).lexically_normal();
        if (std::ranges::find(options.using_chain, path) != options.using_chain.end()) {
            std::string chain;
            for (const auto& p : options.using_chain) chain += shown(p) + " → ";
            return std::unexpected{SigError{use.line, 0, say("pipeline.blocks.use_cycle", fmt::arg("chain", chain + shown(path)))}};
        }
        std::ifstream in{path, std::ios::binary};
        if (!in) return std::unexpected{SigError{use.line, 0, say("pipeline.blocks.use_missing", fmt::arg("path", use.path))}};
        const std::string lib_text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        auto chain = options.using_chain;
        chain.push_back(path);
        auto lib = load_sig(lib_text, {path.parent_path(), chain});
        if (!lib) return std::unexpected{SigError{use.line, 0, shown(path) + ":" + std::to_string(lib.error().line) + ": " + lib.error().message}};
        if (!lib->doc.nodes.empty() || !lib->doc.edges.empty() || !lib->doc.probes.empty())
            return std::unexpected{SigError{use.line, 0, say("pipeline.blocks.library_nodes", fmt::arg("file", shown(path)))}};
        for (auto* list : {&lib->doc.imported, &lib->doc.blocks})
            for (auto& b : *list) {
                if (b.origin.empty()) b.origin = path;
                const auto taken = [&](const std::vector<BlockDef>& defs) { return std::ranges::any_of(defs, [&](const BlockDef& d) { return d.name == b.name; }); };
                if (taken(out.doc.blocks) || taken(out.doc.imported))
                    return std::unexpected{SigError{use.line, 0, say("pipeline.blocks.use_clash", fmt::arg("name", b.name), fmt::arg("file", shown(path)))}};
                out.doc.imported.push_back(std::move(b));
            }
    }
    for (const auto& b : out.doc.blocks)
        if (find_block_type(b.name)) out.notes.push_back(say("pipeline.blocks.shadows", fmt::arg("name", b.name)));
    return out;
}

std::vector<std::filesystem::path> used_files(std::string_view text, const std::filesystem::path& base_dir) {
    std::vector<std::filesystem::path> out;
    const std::function<void(std::string_view, const std::filesystem::path&)> walk = [&](std::string_view source, const std::filesystem::path& dir) {
        const auto doc = parse_sig(source);
        if (!doc) return;
        for (const auto& use : doc->uses) {
            auto path = utf8_path(use.path);
            if (path.is_relative()) path = dir / path;
            path = std::filesystem::absolute(path).lexically_normal();
            if (std::ranges::find(out, path) != out.end()) continue;
            out.push_back(path);
            std::ifstream in{path, std::ios::binary};
            if (!in) continue;
            const std::string lib{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
            walk(lib, path.parent_path());
        }
    };
    walk(text, base_dir);
    return out;
}

}  // namespace signals::pipeline
