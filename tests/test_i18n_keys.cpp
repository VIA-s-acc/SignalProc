#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "i18n/i18n.hpp"

TEST_CASE("every tr key used in the sources exists", "[i18n]") {
    const std::filesystem::path root{SIGNALS_SOURCE_DIR};
    std::ifstream dictionary_file{root / "resources" / "i18n" / "ru.json"};
    const auto dictionary = nlohmann::json::parse(dictionary_file);
    const std::regex literal{R"re(tr\("([^"]+)"\))re"};
    std::size_t keys = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "src")) {
        if (entry.path().extension() != ".cpp") continue;
        std::ifstream in{entry.path()};
        const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        for (std::sregex_iterator it{text.begin(), text.end(), literal}, end; it != end; ++it) {
            INFO(entry.path().filename().string() << ": " << (*it)[1].str());
            CHECK(dictionary.contains((*it)[1].str()));
            ++keys;
        }
    }
    CHECK(keys > 50);
    for (const char* code : {"invalid_argument", "level_out_of_range", "length_mismatch", "not_perfect_reconstruction",
                             "unknown_wavelet", "no_convergence", "io_error", "cancelled"}) {
        INFO(code);
        CHECK(dictionary.contains(std::string{"error."} + code));
    }
    for (const char* kind : {"orthogonal", "linear_phase", "other"}) CHECK(dictionary.contains(std::string{"design.kind."} + kind));
}

namespace {

nlohmann::json load_dictionary(const char* name) {
    std::ifstream in{std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "i18n" / name};
    return nlohmann::json::parse(in);
}

bool has_cyrillic(const std::string& text) {
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead == 0xD0 || lead == 0xD1) return true;
    }
    return false;
}

std::set<std::string> placeholders(const std::string& text) {
    const std::regex brace{R"(\{([a-z_0-9]*)(:[^}]*)?\})"};
    std::set<std::string> out;
    for (std::sregex_iterator it{text.begin(), text.end(), brace}, end; it != end; ++it) out.insert((*it)[1].str());
    return out;
}

}  // namespace

TEST_CASE("the English dictionary covers every Russian key with the same placeholders", "[i18n]") {
    const auto ru = load_dictionary("ru.json");
    const auto en = load_dictionary("en.json");
    for (const auto& [key, value] : ru.items()) {
        INFO(key);
        REQUIRE(en.contains(key));
        const auto english = en[key].get<std::string>();
        CHECK(placeholders(english) == placeholders(value.get<std::string>()));
        CHECK_FALSE(has_cyrillic(english));
    }
}

TEST_CASE("tr follows the chosen language and falls back to Russian", "[i18n]") {
    using signals::Language;
    const signals::LanguageScope scope{Language::en};
    CHECK(signals::language() == Language::en);
    CHECK(signals::tr("tui.cmd.quit") != std::string{"tui.cmd.quit"});
    CHECK_FALSE(has_cyrillic(signals::tr("tui.cmd.quit")));
    signals::set_language(Language::ru);
    CHECK(has_cyrillic(signals::tr("tui.cmd.quit")));
    CHECK(signals::tr("no.such.key") == "no.such.key");
}

TEST_CASE("language names parse in both spellings", "[i18n]") {
    using signals::Language;
    CHECK(signals::parse_language("en") == Language::en);
    CHECK(signals::parse_language("EN_us.UTF-8") == Language::en);
    CHECK(signals::parse_language("english") == Language::en);
    CHECK(signals::parse_language("ru") == Language::ru);
    CHECK(signals::parse_language("ru_RU.UTF-8") == Language::ru);
    CHECK(signals::parse_language("русский") == Language::ru);
    CHECK_FALSE(signals::parse_language("de"));
    CHECK(signals::language_code(Language::en) == "en");
}
