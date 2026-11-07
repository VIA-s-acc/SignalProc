#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/bank.hpp"
#include "core/error.hpp"
#include "core/extension.hpp"
#include "core/progress.hpp"

namespace signals::core {

struct PacketNode {
    std::size_t level = 0, index = 0;
    friend bool operator==(PacketNode, PacketNode) = default;
};

std::string packet_path(PacketNode n);
std::size_t natural_index_at(std::size_t level, std::size_t freq_position);
std::size_t freq_position(PacketNode n);

struct PacketTree {
    std::vector<std::vector<std::vector<double>>> nodes;
    std::size_t levels() const { return nodes.empty() ? 0 : nodes.size() - 1; }
    const std::vector<double>& at(PacketNode n) const { return nodes[n.level][n.index]; }
};

Result<PacketTree> wp_decompose(std::span<const double> x, const FilterBank& bank, std::size_t levels, Extension mode,
                                ProgressSink* progress = nullptr);
bool admissible(std::span<const PacketNode> basis, std::size_t levels);
Result<std::vector<double>> wp_reconstruct(const PacketTree& tree, std::span<const PacketNode> basis, const FilterBank& bank,
                                           Extension mode);

enum class PacketCost { shannon, log_energy };
double packet_cost(std::span<const double> c, PacketCost cost, double norm2);
std::vector<PacketNode> best_basis(const PacketTree& tree, PacketCost cost);
std::vector<PacketNode> dwt_basis(std::size_t levels);
std::vector<PacketNode> level_basis(std::size_t level);
void sort_by_frequency(std::vector<PacketNode>& basis);
Result<Laurent> packet_filter(const FilterBank& bank, PacketNode node);

}  // namespace signals::core
