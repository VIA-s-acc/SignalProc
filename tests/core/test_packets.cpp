#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "core/check.hpp"
#include "core/extension.hpp"
#include "core/metrics.hpp"
#include "core/packets.hpp"
#include "support/banks.hpp"
#include "support/golden.hpp"

using namespace signals::core;
using signals::test::golden_bank;
using signals::test::max_abs;
using signals::test::signal_of_length;

namespace {

PacketNode node_of(const std::string& path) {
    PacketNode n{path.size(), 0};
    for (const char c : path) n.index = n.index * 2 + (c == 'd' ? 1 : 0);
    return n;
}

double basis_cost(const PacketTree& tree, const std::vector<PacketNode>& basis, PacketCost cost) {
    double norm2 = 0;
    for (const double v : tree.at({0, 0})) norm2 += v * v;
    double total = 0;
    for (const auto& n : basis) total += packet_cost(tree.at(n), cost, norm2);
    return total;
}

void all_bases(PacketNode n, std::size_t levels, const std::function<void(std::vector<PacketNode>)>& out) {
    out({n});
    if (n.level == levels) return;
    all_bases({n.level + 1, 2 * n.index}, levels, [&](std::vector<PacketNode> left) {
        all_bases({n.level + 1, 2 * n.index + 1}, levels, [&](std::vector<PacketNode> right) {
            auto both = left;
            both.insert(both.end(), right.begin(), right.end());
            out(std::move(both));
        });
    });
}

std::vector<PacketNode> random_basis(std::size_t levels, std::mt19937& rng) {
    std::vector<PacketNode> out;
    std::function<void(PacketNode)> walk = [&](PacketNode n) {
        if (n.level == levels || (n.level > 0 && std::uniform_int_distribution<int>{0, 2}(rng) == 0)) {
            out.push_back(n);
            return;
        }
        walk({n.level + 1, 2 * n.index});
        walk({n.level + 1, 2 * n.index + 1});
    };
    walk({0, 0});
    return out;
}

}  // namespace

TEST_CASE("wavelet packets match pywt", "[packets][golden]") {
    auto g = signals::test::load_golden("packets.json");
    REQUIRE(g["cases"].size() > 0);
    for (auto& c : g["cases"]) {
        const auto n = c["n"].get<std::size_t>();
        const auto x = g["signals"][std::to_string(n)].get<std::vector<double>>();
        const auto b = golden_bank(c["wavelet"].get<std::string>());
        const auto mode = parse_extension(c["mode"].get<std::string>());
        REQUIRE(mode);
        const auto level = c["level"].get<std::size_t>();
        INFO(c["wavelet"] << " " << c["mode"] << " n=" << n << " level=" << level);
        const auto tree = wp_decompose(x, b, level, *mode);
        REQUIRE(tree);
        CHECK(tree->levels() == level);
        const double tol = default_tolerance(level, b.length(), max_abs(x));
        for (auto& [path, data] : c["nodes"].items()) {
            INFO(path);
            CHECK(max_abs_error(tree->at(node_of(path)), data.get<std::vector<double>>()) <= tol);
        }
        std::vector<PacketNode> basis;
        for (const auto& p : c["basis"]) basis.push_back(node_of(p.get<std::string>()));
        REQUIRE(admissible(basis, level));
        const auto y = wp_reconstruct(*tree, basis, b, *mode);
        REQUIRE(y);
        const auto expected = c["reconstruct"].get<std::vector<double>>();
        CHECK(max_abs_error(*y, expected) <= tol);
        CHECK(max_abs_error(*y, x) <= tol + 2 * max_abs_error(expected, x));
    }
}

TEST_CASE("packet paths and the frequency order follow pywt", "[packets]") {
    CHECK(packet_path({2, 1}) == "ad");
    CHECK(packet_path({3, 6}) == "dda");
    CHECK(packet_path({0, 0}).empty());
    CHECK(std::vector<std::size_t>{natural_index_at(2, 0), natural_index_at(2, 1), natural_index_at(2, 2), natural_index_at(2, 3)} ==
          std::vector<std::size_t>{0, 1, 3, 2});
    for (std::size_t j = 0; j < 7; ++j)
        for (std::size_t k = 0; k < (std::size_t{1} << j); ++k) {
            const PacketNode n{j, natural_index_at(j, k)};
            CHECK(freq_position(n) == k);
            const auto lo = freq_position({j + 1, 2 * n.index}), hi = freq_position({j + 1, 2 * n.index + 1});
            CHECK(std::min(lo, hi) == 2 * k);
            CHECK(std::max(lo, hi) == 2 * k + 1);
        }
}

TEST_CASE("any admissible packet basis reconstructs the signal", "[packets]") {
    const auto x = signal_of_length(77);
    const auto b = golden_bank("db2");
    for (const auto mode : {Extension::symmetric, Extension::periodization, Extension::zero}) {
        const auto tree = wp_decompose(x, b, 3, mode);
        REQUIRE(tree);
        const double tol = default_tolerance(3, b.length(), max_abs(x));
        std::mt19937 rng{7};
        std::vector<std::vector<PacketNode>> bases{dwt_basis(3), level_basis(3), level_basis(0)};
        for (int i = 0; i < 20; ++i) bases.push_back(random_basis(3, rng));
        for (const auto& basis : bases) {
            REQUIRE(admissible(basis, 3));
            const auto y = wp_reconstruct(*tree, basis, b, mode);
            REQUIRE(y);
            CHECK(max_abs_error(*y, x) <= tol);
        }
    }
}

TEST_CASE("the best basis is the cheapest admissible basis", "[packets]") {
    const auto x = signal_of_length(64);
    for (const char* name : {"haar", "db2"})
        for (const auto cost : {PacketCost::shannon, PacketCost::log_energy})
            for (std::size_t levels = 1; levels <= 4; ++levels) {
                INFO(name << " levels=" << levels);
                const auto tree = wp_decompose(x, golden_bank(name), levels, Extension::periodization);
                REQUIRE(tree);
                const auto best = best_basis(*tree, cost);
                REQUIRE(admissible(best, levels));
                for (std::size_t i = 1; i < best.size(); ++i)
                    CHECK((freq_position(best[i - 1]) << (levels - best[i - 1].level)) < (freq_position(best[i]) << (levels - best[i].level)));
                const double found = basis_cost(*tree, best, cost);
                double minimum = std::numeric_limits<double>::infinity();
                std::size_t count = 0;
                all_bases({0, 0}, levels, [&](std::vector<PacketNode> basis) {
                    minimum = std::min(minimum, basis_cost(*tree, basis, cost));
                    ++count;
                });
                CHECK(count == std::vector<std::size_t>{1, 2, 5, 26, 677}[levels]);
                CHECK(found <= minimum + 1e-12 * std::max(1.0, std::abs(minimum)));
                CHECK(found <= basis_cost(*tree, level_basis(0), cost) + 1e-12);
                CHECK(found <= basis_cost(*tree, level_basis(levels), cost) + 1e-12);
            }
}

TEST_CASE("admissible rejects overlaps, holes and nodes beyond the tree", "[packets]") {
    CHECK(admissible(std::vector<PacketNode>{{1, 0}, {2, 2}, {2, 3}}, 2));
    CHECK_FALSE(admissible(std::vector<PacketNode>{{1, 0}, {2, 0}, {1, 1}}, 2));
    CHECK_FALSE(admissible(std::vector<PacketNode>{{1, 0}}, 2));
    CHECK_FALSE(admissible(std::vector<PacketNode>{{1, 0}, {3, 4}, {3, 5}, {2, 3}}, 2));
    CHECK_FALSE(admissible(std::vector<PacketNode>{{1, 0}, {1, 2}}, 2));
    CHECK_FALSE(admissible(std::vector<PacketNode>{}, 2));
}

TEST_CASE("packets deeper than the dwt limit still reconstruct", "[packets]") {
    const auto x = signal_of_length(5);
    const auto b = golden_bank("db4");
    const auto tree = wp_decompose(x, b, 4, Extension::symmetric);
    REQUIRE(tree);
    CHECK(tree->levels() == 4);
    const auto y = wp_reconstruct(*tree, level_basis(4), b, Extension::symmetric);
    REQUIRE(y);
    CHECK(max_abs_error(*y, x) <= default_tolerance(4, b.length(), max_abs(x)));
    CHECK_FALSE(wp_decompose(std::vector<double>{}, b, 2, Extension::symmetric));
    CHECK_FALSE(wp_decompose(x, b, 40, Extension::symmetric));
}
