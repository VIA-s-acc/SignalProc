#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "pipeline/graph.hpp"
#include "pipeline/layout.hpp"
#include "pipeline/sig.hpp"

using namespace signals::pipeline;

namespace {

Graph graph_of(std::string_view text) {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    auto g = build_graph(*doc, ".");
    REQUIRE(g);
    return *g;
}

}  // namespace

TEST_CASE("a tree lays out in layers without crossings", "[pipeline][layout]") {
    const auto g = graph_of("node src = gen()\nnode a = gain()\nnode b = gain()\nnode c = gain()\nsrc -> a\nsrc -> b\na -> c\n");
    const auto l = layout_graph(g);
    REQUIRE(l.layers.size() == 3);
    CHECK(l.items[l.item_of_node[0]].layer == 0);
    CHECK(l.items[l.item_of_node[3]].layer == 2);
    CHECK(crossings(l) == 0);
}

TEST_CASE("a long wire gets a dummy point in every skipped layer", "[pipeline][layout]") {
    const auto g = graph_of("node s = gen()\nnode a = gain()\nnode b = gain()\nnode c = check()\ns -> a -> b -> c.in\ns -> c.ref\n");
    const auto l = layout_graph(g);
    REQUIRE(l.layers.size() == 4);
    int dummies = 0;
    for (const auto& it : l.items)
        if (it.node < 0) {
            ++dummies;
            CHECK((it.layer == 1 || it.layer == 2));
            CHECK(it.next.size() == 1);
        }
    CHECK(dummies == 2);
}

TEST_CASE("barycenters undo a crossing that declaration order makes", "[pipeline][layout]") {
    const auto g = graph_of("node a1 = gen()\nnode b1 = gen()\nnode a2 = gain()\nnode b2 = gain()\na1 -> b2\nb1 -> a2\n");
    const auto l = layout_graph(g);
    CHECK(crossings(l) == 0);
    CHECK(l.items[l.item_of_node[3]].order == 0);
}
