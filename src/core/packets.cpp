#include "core/packets.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "core/dwt.hpp"
#include "core/freq.hpp"

namespace signals::core {

namespace {

constexpr std::size_t kMaxTreeValues = std::size_t{1} << 28;
constexpr std::size_t kMaxLevels = 62;

DwtOptions packet_options(Extension mode) { return {mode, true}; }

std::size_t child_length(std::size_t n, std::size_t filter_len, Extension mode) {
    return mode == Extension::periodization ? (n + 1) / 2 : dwt_coeff_len(n, filter_len, mode);
}

}  // namespace

std::string packet_path(PacketNode n) {
    std::string out(n.level, 'a');
    for (std::size_t i = 0; i < n.level; ++i)
        if ((n.index >> (n.level - 1 - i)) & 1U) out[i] = 'd';
    return out;
}

std::size_t natural_index_at(std::size_t, std::size_t k) { return k ^ (k >> 1); }

std::size_t freq_position(PacketNode n) {
    std::size_t k = n.index;
    for (std::size_t s = 1; s < 64; s <<= 1) k ^= k >> s;
    return k;
}

Result<PacketTree> wp_decompose(std::span<const double> x, const FilterBank& bank, std::size_t levels, Extension mode,
                                ProgressSink* progress) {
    if (x.empty()) return make_error(Errc::invalid_argument, "packets: empty signal");
    if (levels > kMaxLevels || x.size() > kMaxTreeValues) return make_error(Errc::level_out_of_range, "packets: too many levels for the signal length");
    PacketTree tree;
    tree.nodes.push_back({std::vector<double>(x.begin(), x.end())});
    std::size_t total = x.size();
    for (std::size_t j = 0; j < levels; ++j) {
        const auto& parents = tree.nodes[j];
        const auto next = child_length(parents.front().size(), bank.length(), mode);
        const auto count = std::size_t{2} << j;
        if (next > 0 && count > (kMaxTreeValues - total) / next)
            return make_error(Errc::level_out_of_range, "packets: the tree for " + std::to_string(levels) + " levels does not fit in memory");
        total += count * next;
        std::vector<std::vector<double>> children;
        children.reserve(count);
        for (const auto& parent : parents) {
            auto level = dwt_level<double>(parent, bank, packet_options(mode), progress);
            if (!level) return std::unexpected{level.error()};
            children.push_back(std::move(level->approx));
            children.push_back(std::move(level->detail));
        }
        tree.nodes.push_back(std::move(children));
    }
    return tree;
}

bool admissible(std::span<const PacketNode> basis, std::size_t levels) {
    if (basis.empty() || levels > kMaxLevels) return false;
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    spans.reserve(basis.size());
    for (const auto& n : basis) {
        if (n.level > levels || n.index >= (std::size_t{1} << n.level)) return false;
        const auto width = std::size_t{1} << (levels - n.level);
        spans.emplace_back(n.index * width, (n.index + 1) * width);
    }
    std::ranges::sort(spans);
    std::size_t at = 0;
    for (const auto& [lo, hi] : spans) {
        if (lo != at) return false;
        at = hi;
    }
    return at == std::size_t{1} << levels;
}

Result<std::vector<double>> wp_reconstruct(const PacketTree& tree, std::span<const PacketNode> basis, const FilterBank& bank,
                                           Extension mode) {
    if (!admissible(basis, tree.levels())) return make_error(Errc::invalid_argument, "packets: the basis does not tile the tree");
    std::map<std::pair<std::size_t, std::size_t>, std::vector<double>> live;
    std::size_t deepest = 0;
    for (const auto& n : basis) {
        live.emplace(std::pair{n.level, n.index}, tree.at(n));
        deepest = std::max(deepest, n.level);
    }
    for (std::size_t j = deepest; j > 0; --j) {
        auto it = live.lower_bound({j, 0});
        while (it != live.end() && it->first.first == j) {
            const auto first = it++;
            const auto second = it++;
            const auto parent = first->first.second / 2;
            auto rec = idwt_level<double>(first->second, second->second, bank, packet_options(mode));
            if (!rec) return std::unexpected{rec.error()};
            rec->resize(std::min(rec->size(), tree.nodes[j - 1][parent].size()));
            live.emplace(std::pair{j - 1, parent}, std::move(*rec));
            live.erase(first);
            live.erase(second);
        }
    }
    return std::move(live.begin()->second);
}

double packet_cost(std::span<const double> c, PacketCost cost, double norm2) {
    double total = 0;
    for (const double v : c) {
        if (v == 0) continue;
        if (cost == PacketCost::log_energy) {
            total += std::log(v * v);
        } else if (norm2 > 0) {
            const double p = v * v / norm2;
            total -= p * std::log(p);
        }
    }
    return total;
}

std::vector<PacketNode> best_basis(const PacketTree& tree, PacketCost cost) { return best_basis(std::span{&tree, 1}, cost); }

std::vector<PacketNode> best_basis(std::span<const PacketTree> trees, PacketCost cost) {
    if (trees.empty()) return {{0, 0}};
    const auto levels = trees.front().levels();
    std::vector<double> norm2(trees.size(), 0.0);
    for (std::size_t t = 0; t < trees.size(); ++t)
        for (const double v : trees[t].at({0, 0})) norm2[t] += v * v;
    std::vector<std::vector<double>> best(levels + 1);
    std::vector<std::vector<bool>> split(levels + 1);
    for (std::size_t j = levels + 1; j-- > 0;) {
        const auto count = trees.front().nodes[j].size();
        best[j].resize(count);
        split[j].assign(count, false);
        for (std::size_t n = 0; n < count; ++n) {
            double own = 0;
            for (std::size_t t = 0; t < trees.size(); ++t) own += packet_cost(trees[t].nodes[j][n], cost, norm2[t]);
            best[j][n] = own;
            if (j == levels) continue;
            const double children = best[j + 1][2 * n] + best[j + 1][2 * n + 1];
            if (children < own) {
                best[j][n] = children;
                split[j][n] = true;
            }
        }
    }
    std::vector<PacketNode> out;
    std::vector<PacketNode> stack{{0, 0}};
    while (!stack.empty()) {
        const auto n = stack.back();
        stack.pop_back();
        if (split[n.level][n.index]) {
            stack.push_back({n.level + 1, 2 * n.index + 1});
            stack.push_back({n.level + 1, 2 * n.index});
        } else {
            out.push_back(n);
        }
    }
    sort_by_frequency(out);
    return out;
}

std::vector<PacketNode> dwt_basis(std::size_t levels) {
    if (levels == 0) return {{0, 0}};
    std::vector<PacketNode> out{{levels, 0}};
    for (std::size_t j = levels; j >= 1; --j) out.push_back({j, 1});
    return out;
}

std::vector<PacketNode> level_basis(std::size_t level) {
    std::vector<PacketNode> out;
    for (std::size_t k = 0; k < (std::size_t{1} << level); ++k) out.push_back({level, natural_index_at(level, k)});
    return out;
}

Result<Laurent> packet_filter(const FilterBank& bank, PacketNode node) {
    if (node.level == 0 || node.level > 16 || node.index >> node.level != 0)
        return make_error(Errc::level_out_of_range, "packet_filter: the node is not in a tree of 1..16 levels");
    Laurent g{{1.0}, 0};
    for (std::size_t i = 0; i < node.level; ++i) {
        const bool detail = (node.index >> (node.level - 1 - i)) & 1U;
        g = multiply(dilate(detail ? bank.h1 : bank.h0, std::size_t{1} << i), g);
    }
    return g;
}

void sort_by_frequency(std::vector<PacketNode>& basis) {
    std::size_t deepest = 0;
    for (const auto& n : basis) deepest = std::max(deepest, n.level);
    std::ranges::sort(basis, {}, [deepest](PacketNode n) { return freq_position(n) << (deepest - n.level); });
}

}  // namespace signals::core
