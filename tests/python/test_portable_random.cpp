#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/random.hpp"
#include "io/npy.hpp"
#include "python/form.hpp"
#include "support/paths.hpp"

namespace {

std::string python_exe() {
    const char* p = std::getenv("SIGNALS_PYTHON");
    return p ? p : "python3";
}

bool python_ready() { return std::system((python_exe() + " -c \"import numpy\" > " + std::string{SIGNALS_NULL_DEVICE} + " 2>&1").c_str()) == 0; }

std::vector<double> normals(std::uint64_t seed, std::size_t n) {
    signals::core::NormalDraws draws{seed};
    std::vector<double> out;
    for (std::size_t k = 0; k < n; ++k) out.push_back(draws.next());
    return out;
}

std::vector<double> tpdf(std::uint64_t seed, std::size_t n) {
    signals::core::TpdfDraws draws{seed};
    std::vector<double> out;
    for (std::size_t k = 0; k < n; ++k) out.push_back(draws.next());
    return out;
}

std::vector<double> uniform(std::uint64_t seed, std::size_t n) {
    std::mt19937_64 rng{seed};
    std::vector<double> out;
    for (std::size_t k = 0; k < n; ++k) out.push_back(signals::core::uniform53(rng));
    return out;
}

}  // namespace

TEST_CASE("the portable generator is a script function", "[py-random]") { CHECK(signals::python::find_python_function("portable_random")); }

TEST_CASE("numpy draws equal the pinned C++ draws", "[py-random]") {
    if (!python_ready()) {
        if (std::getenv("SIGNALS_REQUIRE_BRIDGE")) FAIL("python3 with numpy is required: SIGNALS_REQUIRE_BRIDGE is set");
        SKIP("no python3 with numpy");
    }
    const auto dir = signals::test::temp_path("py-random");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto put = [&](const std::string& name, std::vector<double> v) { REQUIRE(signals::io::write_npy(dir / (name + ".npy"), signals::io::np_vector(std::move(v)))); };
    put("normal_11_long", normals(11, 400000));
    put("normal_7_odd", normals(7, 5));
    put("normal_0_zero", normals(0, 313));
    put("normal_18446744073709551615_max", normals(18446744073709551615ULL, 1000));
    put("tpdf_11_long", tpdf(11, 100001));
    put("uniform_5_block", uniform(5, 937));
    const auto root = std::filesystem::path{SIGNALS_SOURCE_DIR};
    const auto log = dir / "check.log";
    const auto cmd = python_exe() + " -I \"" + (root / "tools" / "bridge" / "check_random.py").string() + "\" \"" + (root / "resources" / "python" / "portable_random.py").string() +
                     "\" \"" + dir.string() + "\" > \"" + log.string() + "\" 2>&1";
    const int code = std::system(cmd.c_str());
    std::ifstream in{log};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    INFO(text);
    CHECK(code == 0);
}
