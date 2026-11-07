#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "i18n/i18n.hpp"
#include "pipeline/docs.hpp"

namespace {

std::string read(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("the block reference lists every block with its ports and parameters", "[pipeline][docs]") {
    const auto md = signals::pipeline::blocks_markdown();
    for (const char* id : {"`gain`", "`lift53`", "`xcorr`", "`export`", "`denoise`"}) CHECK(md.find(id) != std::string::npos);
    CHECK(md.find("Усиление") != std::string::npos);
    CHECK(md.find("`levels` = 4 (1…30)") != std::string::npos);
    CHECK(md.find("`format` = csv (csv, json)") != std::string::npos);
    const signals::LanguageScope en{signals::Language::en};
    CHECK(signals::pipeline::blocks_markdown().find("Gain") != std::string::npos);
}

TEST_CASE("docs/user/blocks.md is the generated reference", "[pipeline][docs]") {
    const std::filesystem::path dir = std::filesystem::path{SIGNALS_SOURCE_DIR} / "docs" / "user";
    INFO("regenerate with: signals --lang ru blocks --markdown > docs/user/blocks.md and signals --lang en blocks --markdown > docs/user/blocks.en.md");
    CHECK(read(dir / "blocks.md") == signals::pipeline::blocks_markdown());
    const signals::LanguageScope en{signals::Language::en};
    CHECK(read(dir / "blocks.en.md") == signals::pipeline::blocks_markdown());
}

TEST_CASE("m7a review: markdown cells escape pipes once", "[pipeline][docs]") {
    CHECK(signals::pipeline::markdown_cell("a | b") == "a \\| b");
    CHECK(signals::pipeline::markdown_cell("one\ntwo") == "one two");
}

TEST_CASE("m7b: the English block reference has no Russian", "[pipeline][docs]") {
    const signals::LanguageScope en{signals::Language::en};
    CHECK(signals::pipeline::blocks_markdown().find_first_of("\xD0\xD1") == std::string::npos);
}
