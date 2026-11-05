#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include <nlohmann/json.hpp>

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
                             "unknown_wavelet", "no_convergence", "io_error"}) {
        INFO(code);
        CHECK(dictionary.contains(std::string{"error."} + code));
    }
    for (const char* kind : {"orthogonal", "linear_phase", "other"}) CHECK(dictionary.contains(std::string{"design.kind."} + kind));
}
