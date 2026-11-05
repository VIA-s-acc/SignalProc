#include <catch2/catch_test_macros.hpp>

#include "support/golden.hpp"

TEST_CASE("golden files are complete", "[golden]") {
    auto f = signals::test::load_golden("filters.json");
    CHECK(f["pywt_version"] == "1.8.0");
    CHECK(f["wavelets"].size() == 106);
    CHECK(f["wavelets"][0]["name"] == "bior1.1");
    auto d = signals::test::load_golden("dwt.json");
    CHECK(d["cases"].size() == 106 * 9 + 10 * 9 * 6);
    CHECK(signals::test::golden_signal(d, 37).size() == 37);
}
