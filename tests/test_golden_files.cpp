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

TEST_CASE("m2 golden files are complete", "[golden]") {
    CHECK(signals::test::load_golden("wav.json")["cases"].size() == 9);
    CHECK(signals::test::load_golden("denoise.json")["cases"].size() == 2 * 3 * 2 * 2 + 2);
    CHECK(signals::test::load_golden("threshold.json")["cases"].size() == 5 * 2 * 2 + 1);
    CHECK(signals::test::load_golden("freqz.json")["cases"].size() == 6);
}
