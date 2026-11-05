#include "support/golden.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace signals::test {

nlohmann::json load_golden(std::string_view file_name) {
    const auto path = std::filesystem::path{SIGNALS_GOLDEN_DIR} / file_name;
    std::ifstream in{path};
    if (!in) throw std::runtime_error("golden file missing: " + path.string());
    return nlohmann::json::parse(in);
}

std::vector<double> golden_signal(const nlohmann::json& dwt_json, std::size_t n) {
    return dwt_json.at("signals").at(std::to_string(n)).get<std::vector<double>>();
}

}  // namespace signals::test
