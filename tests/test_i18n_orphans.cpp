#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "pipeline/block.hpp"

namespace {

bool known(std::string_view key) {
    using signals::pipeline::find_block_type;
    for (const std::string_view prefix : {"pipeline.block.", "pipeline.about."})
        if (key.starts_with(prefix)) return find_block_type(key.substr(prefix.size())) != nullptr;
    const std::string_view prefix = "pipeline.param.";
    if (!key.starts_with(prefix)) return true;
    auto rest = key.substr(prefix.size());
    const auto dot = rest.find('.');
    if (dot == std::string_view::npos) return false;
    const auto* type = find_block_type(rest.substr(0, dot));
    if (!type) return false;
    rest = rest.substr(dot + 1);
    const auto second = rest.find('.');
    const auto param = rest.substr(0, second);
    const auto spec = std::ranges::find_if(type->params, [&](const auto& p) { return p.name == param; });
    if (spec == type->params.end()) return false;
    if (second == std::string_view::npos) return true;
    return spec->choices.empty() || std::ranges::find(spec->choices, rest.substr(second + 1)) != spec->choices.end();
}

}  // namespace

TEST_CASE("every per-block interface key matches a block, a parameter or a choice", "[i18n]") {
    for (const auto* lang : {"ru", "en"}) {
        std::ifstream in{std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "i18n" / (std::string{lang} + ".json")};
        const auto keys = nlohmann::json::parse(in);
        for (const auto& [key, value] : keys.items()) {
            INFO(lang << ": " << key);
            CHECK(known(key));
        }
    }
}
