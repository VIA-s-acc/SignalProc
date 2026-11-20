#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("nothing outside pipeline calls parse_sig", "[no-raw-parse]") {
    const auto src = std::filesystem::path{SIGNALS_SOURCE_DIR} / "src";
    for (const auto& e : std::filesystem::recursive_directory_iterator{src}) {
        if (!e.is_regular_file()) continue;
        const auto rel = e.path().lexically_relative(src).generic_string();
        if (rel.starts_with("pipeline/")) continue;
        std::ifstream in{e.path(), std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        INFO(rel);
        CHECK(text.find("parse_sig(") == std::string::npos);
    }
}
