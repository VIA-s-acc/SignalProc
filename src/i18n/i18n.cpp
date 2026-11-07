#include "i18n/i18n.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>

#include <nlohmann/json.hpp>

extern const char kSignalsRuJson[];
extern const char kSignalsEnJson[];

namespace signals {
namespace {

std::atomic<Language> g_language{Language::ru};

const nlohmann::json& dictionary(Language language) {
    static const nlohmann::json ru = nlohmann::json::parse(kSignalsRuJson);
    static const nlohmann::json en = nlohmann::json::parse(kSignalsEnJson);
    return language == Language::en ? en : ru;
}

std::optional<std::string> lookup(Language language, std::string_view key) {
    const auto& d = dictionary(language);
    const auto it = d.find(key);
    if (it == d.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

}  // namespace

std::string tr(std::string_view key) {
    const auto current = language();
    if (auto text = lookup(current, key)) return *text;
    if (current != Language::ru)
        if (auto text = lookup(Language::ru, key)) return *text;
    return std::string{key};
}

std::optional<std::string> tr_opt(std::string_view key) {
    if (auto text = lookup(language(), key)) return std::string{*text};
    return std::nullopt;
}

std::string tr_count(std::string_view key, long long n, std::string_view shown) {
    const auto abs = n < 0 ? -n : n;
    const char* form = ".many";
    if (language() == Language::en) {
        if (abs == 1) form = ".one";
    } else if (abs % 10 == 1 && abs % 100 != 11) {
        form = ".one";
    } else if (abs % 10 >= 2 && abs % 10 <= 4 && (abs % 100 < 12 || abs % 100 > 14)) {
        form = ".few";
    }
    auto text = tr(std::string{key} + form);
    const auto number = shown.empty() ? std::to_string(n) : std::string{shown};
    if (const auto at = text.find("{n}"); at != std::string::npos) text.replace(at, 3, number);
    return text;
}

void set_language(Language language) { g_language.store(language); }

Language language() { return g_language.load(); }

std::optional<Language> parse_language(std::string_view text) {
    std::string lower;
    for (char c : text) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto starts = [&](std::string_view prefix) {
        return lower.starts_with(prefix) && (lower.size() == prefix.size() || lower[prefix.size()] == '_' || lower[prefix.size()] == '-' ||
                                             lower[prefix.size()] == '.');
    };
    if (starts("en") || lower == "english") return Language::en;
    if (starts("ru") || lower == "russian" || text == "русский" || text == "Русский") return Language::ru;
    return std::nullopt;
}

std::string_view language_code(Language language) { return language == Language::en ? "en" : "ru"; }

}  // namespace signals
