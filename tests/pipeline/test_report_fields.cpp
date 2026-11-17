#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/report_fields.hpp"
#include "python/block_graphs.hpp"
#include "support/paths.hpp"

using namespace signals;

TEST_CASE("field patterns match their fields", "[report-fields]") {
    CHECK(pipeline::field_matches("rms_ch{c}", "rms_ch12"));
    CHECK_FALSE(pipeline::field_matches("rms_ch{c}", "rms_chx"));
    CHECK(pipeline::field_matches("peak{k}_hz_ch{c}", "peak3_hz_ch0"));
    CHECK(pipeline::field_matches("c{c}.{band}.energy", "c0.aad.energy"));
    CHECK(pipeline::field_matches("{band}_ch{c}", "below_22hz_ch1"));
    CHECK_FALSE(pipeline::field_matches("max_abs", "max_abs2"));
}

TEST_CASE("every report field of every block is listed in the reference", "[report-fields]") {
    const auto dir = test::temp_path("report-fields");
    test::prepare_block_files(dir);
    for (const auto& t : pipeline::block_types()) {
        INFO(t.id);
        const bool reports = std::ranges::any_of(t.outputs, [](const auto& p) { return p.type == pipeline::PortType::report; });
        const auto patterns = pipeline::report_fields(t.id);
        CHECK(reports == !patterns.empty());
        if (!reports) continue;
        std::set<std::string> seen;
        for (const auto& g : test::block_graphs(t, dir)) {
            const auto loaded = pipeline::load_sig(g.sig);
            REQUIRE(loaded);
            const auto graph = pipeline::build_graph(loaded->doc, dir);
            if (!graph) continue;
            pipeline::ResultCache cache{0};
            core::CancelToken cancel;
            const auto run = pipeline::run_graph(*graph, cache, {}, cancel);
            if (!run) continue;
            for (const auto& nr : run->reports)
                if (graph->nodes.at(static_cast<std::size_t>(std::ranges::find_if(graph->nodes, [&](const auto& n) { return n.id == nr.node; }) - graph->nodes.begin())).type->id == t.id)
                    for (const auto& [name, value] : nr.report.fields) seen.insert(name);
        }
        for (const auto& name : seen) {
            INFO(name);
            CHECK(std::ranges::any_of(patterns, [&](std::string_view p) { return pipeline::field_matches(p, name); }));
        }
    }
}
