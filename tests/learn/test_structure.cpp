#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

#include "learn/structure.hpp"
#include "learn_support.hpp"

using namespace signals::learn;
using signals::test::sig_doc;

namespace {

std::string haar_bank() {
    std::ifstream in{std::string{SIGNALS_SOURCE_DIR} + "/examples/haar_bank.sig"};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("paths through a block", "[learn][structure]") {
    const auto bank = sig_doc(haar_bank());
    CHECK(all_paths_through(bank, {"x", "sum", "downsample"}));
    CHECK_FALSE(all_paths_through(bank, {"x", "sum", "upsample_missing"}));
    CHECK_FALSE(all_paths_through(bank, {"sum", "x", "downsample"}));
    const auto cheat = sig_doc(replaced(haar_bank(), "x -> h1 -> d -> ud -> f1 -> sum.b", "x -> h1 -> d -> ud -> f1\nx -> sum.b"));
    CHECK_FALSE(all_paths_through(cheat, {"x", "sum", "downsample"}));
    const auto bypassed = sig_doc(replaced(haar_bank(), "node a   = downsample(m=2, anti_alias=no, phase=1)",
                                           "node a   = downsample(m=2, anti_alias=no, phase=1) bypass"));
    CHECK_FALSE(all_paths_through(bypassed, {"x", "sum", "downsample"}));
}

TEST_CASE("path existence ignores block types", "[learn][structure]") {
    const auto bank = sig_doc(haar_bank());
    CHECK(path_exists(bank, "x", "sum"));
    CHECK_FALSE(path_exists(bank, "sum", "x"));
    CHECK_FALSE(path_exists(bank, "x", "nope"));
}

TEST_CASE("counting blocks", "[learn][structure]") {
    CHECK(count_blocks(sig_doc(haar_bank()), "downsample") == 2);
    CHECK(count_blocks(sig_doc(haar_bank()), "convolve") == 4);
    CHECK(count_blocks(sig_doc(haar_bank()), "dwt") == 0);
}

TEST_CASE("locked nodes report changed type and params but ignore the input values", "[learn][structure]") {
    const auto start = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=8)
node c = check(kind=pr, tol=1e-12)
)");
    const std::vector<std::string> locked{"x", "c"};
    CHECK(locked_changes(start, start, locked, "x").empty());
    CHECK(locked_changes(start, sig_doc(R"(pipeline "t" v1
node x = samples(values="9 9 9", rate=8)
node c = check(kind=pr, tol=1e-12, delay=0)
)"), locked, "x").empty());
    CHECK(locked_changes(start, sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=16)
node c = check(kind=pr, tol=1)
)"), locked, "x").size() == 2);
    const auto gone = locked_changes(start, sig_doc(R"(pipeline "t" v1
node x = gen(kind=sine)
)"), locked, "x");
    REQUIRE(gone.size() == 2);
    CHECK(gone[0].find("x") != std::string::npos);
    CHECK(gone[1].find("c") != std::string::npos);
}
