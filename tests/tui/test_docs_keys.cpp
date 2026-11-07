#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "i18n/i18n.hpp"
#include "tui/docs.hpp"

namespace {

void check_doc(const std::filesystem::path& path, const std::string& text) {
    if (const char* update = std::getenv("SIGNALS_UPDATE_SNAPSHOTS"); update && std::string_view{update} == "1") {
        std::ofstream{path, std::ios::binary} << text;
        return;
    }
    std::ifstream in{path, std::ios::binary};
    INFO(path.string() << " — rerun the tests with SIGNALS_UPDATE_SNAPSHOTS=1 to regenerate it");
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(text == ss.str());
}

}  // namespace

TEST_CASE("the keys reference covers every tab and sub-tab", "[tui][docs]") {
    const auto md = signals::tui::keys_markdown();
    for (const char* heading : {"## 1 Сигнал", "## 2 Спектр ▸ Ресемплинг", "## 4 Банк ▸ Блок-схема", "## 9 Теория ▸ Учебник"}) {
        INFO(heading);
        CHECK(md.find(heading) != std::string::npos);
    }
    CHECK(md.find("`{ }`") != std::string::npos);
    const signals::LanguageScope en{signals::Language::en};
    const auto english = signals::tui::keys_markdown();
    const auto cyrillic = english.find_first_of("\xD0\xD1");
    INFO(english.substr(cyrillic == std::string::npos ? 0 : (cyrillic > 40 ? cyrillic - 40 : 0), 80));
    CHECK(cyrillic == std::string::npos);
}

TEST_CASE("docs/user/keys.md is the generated reference", "[tui][docs]") {
    const std::filesystem::path dir = std::filesystem::path{SIGNALS_SOURCE_DIR} / "docs" / "user";
    check_doc(dir / "keys.md", signals::tui::keys_markdown());
    const signals::LanguageScope en{signals::Language::en};
    check_doc(dir / "keys.en.md", signals::tui::keys_markdown());
}
