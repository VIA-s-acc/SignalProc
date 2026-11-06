#include "support/golden_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace signals::test {

void check_golden_text(std::string_view name, const std::string& text) {
    const auto path = std::filesystem::path{SIGNALS_GOLDEN_DIR} / (std::string{name} + ".txt");
    if (const char* update = std::getenv("SIGNALS_UPDATE_SNAPSHOTS"); update && std::string_view{update} == "1") {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
        return;
    }
    std::ifstream in{path, std::ios::binary};
    INFO("golden " << path.string() << " — rerun with SIGNALS_UPDATE_SNAPSHOTS=1 to write it, then review it by eye");
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(text == ss.str());
}

}  // namespace signals::test
