#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "pipeline/sig.hpp"
#include "support/paths.hpp"
#include "tui/screens/pipeline_model.hpp"

using namespace signals::tui;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("opening a v1 graph marks it dirty and shows the notice once", "[tui-migrate]") {
    const auto dir = signals::test::temp_path("tui-migrate");
    std::filesystem::create_directories(dir);
    const auto file = dir / "old.sig";
    std::ofstream{file, std::ios::binary} << "pipeline \"t\" v1\nnode x = samples(values=\"1 2\", rate=8)\nnode g = gain(db=-6)\nx -> g\n";
    PipelineModel model;
    REQUIRE(model.load(file));
    CHECK(model.dirty());
    CHECK(model.migrated_from() == 1);
    const auto notice = model.take_migration_notice();
    REQUIRE(notice);
    CHECK_THAT(*notice, ContainsSubstring("v1") && ContainsSubstring("v2"));
    CHECK_FALSE(model.take_migration_notice());
    const auto saved = dir / "saved.sig";
    REQUIRE(model.save(saved));
    std::ifstream in{saved, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text.starts_with("pipeline \"t\" v2"));
}

TEST_CASE("opening a v2 graph shows no migration notice", "[tui-migrate]") {
    PipelineModel model;
    REQUIRE(model.load_text("t", "pipeline \"t\" v2\nnode g = gain()\n"));
    CHECK(model.migrated_from() == signals::pipeline::kSigVersion);
    CHECK_FALSE(model.take_migration_notice());
}
