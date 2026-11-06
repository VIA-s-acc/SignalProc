#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace signals::tui {

struct Selection {
    enum class Kind { none, node, wire } kind = Kind::none;
    std::string node;
    pipeline::Edge wire;
};

class PipelineModel {
public:
    PipelineModel();

    const pipeline::PipelineDoc& doc() const { return doc_; }
    const std::optional<std::filesystem::path>& path() const { return path_; }
    bool dirty() const { return dirty_; }
    std::optional<pipeline::SigError> problem() const { return problem_; }
    const pipeline::Graph* graph() const { return graph_ ? &*graph_ : nullptr; }
    std::filesystem::path base_dir() const;
    std::size_t undo_depth() const { return undo_.size(); }

    core::Result<void> load(const std::filesystem::path& file);
    core::Result<void> save(const std::filesystem::path& file);
    std::string add_node(std::string_view type);
    core::Result<void> insert_into_wire(const pipeline::Edge& wire, std::string_view type);
    core::Result<void> connect(const pipeline::PortRef& from, const pipeline::PortRef& to);
    void disconnect(const pipeline::Edge& wire);
    void remove_node(std::string_view id);
    core::Result<void> set_param(std::string_view node, std::string_view param, std::string_view value);
    core::Result<void> toggle_bypass(std::string_view node);
    void toggle_probe(const pipeline::PortRef& at);
    bool undo();
    bool redo();

    Selection selection;

private:
    core::Result<void> commit(pipeline::PipelineDoc next);
    void rebuild();

    pipeline::PipelineDoc doc_;
    std::optional<std::filesystem::path> path_;
    bool dirty_ = false;
    std::vector<pipeline::PipelineDoc> undo_, redo_;
    std::optional<pipeline::Graph> graph_;
    std::optional<pipeline::SigError> problem_;
};

bool same_port(const pipeline::PortRef& a, const pipeline::PortRef& b);
bool same_wire(const pipeline::Edge& a, const pipeline::Edge& b);

}  // namespace signals::tui
