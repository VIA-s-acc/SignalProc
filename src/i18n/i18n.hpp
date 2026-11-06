#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace signals {

enum class Language { ru, en };

std::string tr(std::string_view key);

void set_language(Language language);
Language language();
std::optional<Language> parse_language(std::string_view text);
std::string_view language_code(Language language);

class LanguageScope {
public:
    explicit LanguageScope(Language language) : saved_{signals::language()} { set_language(language); }
    ~LanguageScope() { set_language(saved_); }
    LanguageScope(const LanguageScope&) = delete;
    LanguageScope& operator=(const LanguageScope&) = delete;

private:
    Language saved_;
};

}  // namespace signals
