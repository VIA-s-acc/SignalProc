#include "i18n/i18n.hpp"

#include <nlohmann/json.hpp>

extern const char kSignalsRuJson[];

namespace signals {

std::string tr(std::string_view key) {
    static const nlohmann::json dictionary = nlohmann::json::parse(kSignalsRuJson);
    auto it = dictionary.find(key);
    if (it == dictionary.end() || !it->is_string()) return std::string{key};
    return it->get<std::string>();
}

}  // namespace signals
