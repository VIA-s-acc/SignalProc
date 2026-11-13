#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "core/progress.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/examples.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace {

std::optional<double> field(const signals::pipeline::RunResult& run, std::string_view node, std::string_view name) {
    for (const auto& r : run.reports)
        if (r.node == node)
            for (const auto& [key, value] : r.report.fields)
                if (key == name && std::holds_alternative<double>(value)) return std::get<double>(value);
    return std::nullopt;
}

}  // namespace

TEST_CASE("the first steps example denoises a generated signal", "[pipeline][examples][start]") {
    const auto examples = signals::pipeline::examples();
    const auto ex = std::find_if(examples.begin(), examples.end(), [](const auto& e) { return e.name == "first_steps"; });
    REQUIRE(ex != examples.end());
    const auto doc = signals::pipeline::parse_sig(ex->text);
    REQUIRE(doc);
    for (const auto& n : doc->nodes) CHECK(n.type != "wav");
    const auto graph = signals::pipeline::build_graph(*doc, {});
    REQUIRE(graph);
    signals::pipeline::ResultCache cache{0};
    const auto run = signals::pipeline::run_graph(*graph, cache, {}, signals::core::CancelToken{});
    REQUIRE(run);
    CHECK(run->probes.contains("noisy"));
    CHECK(run->probes.contains("clean"));
    const auto before = field(*run, "chk_in", "snr_db");
    const auto after = field(*run, "chk_out", "snr_db");
    REQUIRE(before);
    REQUIRE(after);
    CHECK(*after >= *before + 6.0);
}
