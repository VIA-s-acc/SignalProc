#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tui/shell/shell.hpp"

namespace fs = std::filesystem;

namespace {

const fs::path kRoot{SIGNALS_SOURCE_DIR};

std::string read(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

const std::vector<std::pair<std::string, std::string>> kReadmes{{"en", "README.md"}, {"ru", "README.ru.md"}};

constexpr const char* kInstallSh = "curl -fsSL https://raw.githubusercontent.com/VIA-s-acc/SignalProc/main/install.sh | sh";
constexpr const char* kInstallPs = "irm https://raw.githubusercontent.com/VIA-s-acc/SignalProc/main/install.ps1 | iex";
constexpr const char* kBrew = "brew install VIA-s-acc/signals/signals";

}  // namespace

TEST_CASE("readmes show an image of every screen", "[readme]") {
    for (const auto& [lang, name] : kReadmes) {
        INFO(name);
        const auto text = read(kRoot / name);
        REQUIRE_FALSE(text.empty());
        for (const auto route : signals::tui::route_names()) {
            const auto image = "docs/images/" + lang + "/" + std::string{route} + ".svg";
            INFO(image);
            CHECK(fs::exists(kRoot / image));
            CHECK(text.find(image) != std::string::npos);
        }
        for (const char* animation : {"pipeline-run", "learn-check", "designer-pick"}) {
            const auto image = "docs/images/" + lang + "/" + animation + ".svg";
            INFO(image);
            CHECK(fs::exists(kRoot / image));
            CHECK(text.find(image) != std::string::npos);
        }
        const auto head = lines(text);
        bool tour = false;
        for (std::size_t i = 0; i < std::min<std::size_t>(15, head.size()); ++i) tour = tour || head[i].find("docs/images/" + lang + "/tour.svg") != std::string::npos;
        CHECK(tour);
    }
}

TEST_CASE("readmes link each other on the first line", "[readme]") {
    const auto en = lines(read(kRoot / "README.md")), ru = lines(read(kRoot / "README.ru.md"));
    REQUIRE_FALSE(en.empty());
    REQUIRE_FALSE(ru.empty());
    CHECK(en.front().find("(README.ru.md)") != std::string::npos);
    CHECK(ru.front().find("(README.md)") != std::string::npos);
}

TEST_CASE("readmes have the same sections", "[readme]") {
    std::vector<std::size_t> counts;
    for (const auto& [lang, name] : kReadmes) {
        std::size_t n = 0;
        for (const auto& line : lines(read(kRoot / name))) n += line.starts_with("## ") || line.starts_with("### ");
        counts.push_back(n);
    }
    CHECK(counts[0] == counts[1]);
    CHECK(counts[0] >= 10);
}

TEST_CASE("readmes give the one-line installs", "[readme]") {
    for (const auto& [lang, name] : kReadmes) {
        INFO(name);
        const auto text = read(kRoot / name);
        CHECK(text.find(kInstallSh) != std::string::npos);
        CHECK(text.find(kInstallPs) != std::string::npos);
        CHECK(text.find(kBrew) != std::string::npos);
    }
}

TEST_CASE("english readme links english docs", "[readme]") {
    const auto text = read(kRoot / "README.md");
    const std::regex link{R"(\]\((docs/[^)#]+\.md)[^)]*\))"};
    std::size_t checked = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), link); it != std::sregex_iterator(); ++it) {
        const auto target = (*it)[1].str();
        INFO(target);
        CHECK(fs::exists(kRoot / target));
        if (target.starts_with("docs/user/") || target == "docs/02-theory.md" || target == "docs/02-theory.en.md") {
            CHECK(target.ends_with(".en.md"));
            ++checked;
        }
    }
    CHECK(checked >= 4);
}

TEST_CASE("readme links point at files that exist", "[readme]") {
    const std::regex link{R"(\]\(((?:docs|examples|editors|packaging|resources|src|tests|tools)/[^)#\s]*)[^)]*\))"};
    for (const auto& [lang, name] : kReadmes) {
        const auto text = read(kRoot / name);
        for (auto it = std::sregex_iterator(text.begin(), text.end(), link); it != std::sregex_iterator(); ++it) {
            const auto target = (*it)[1].str();
            INFO(name << " -> " << target);
            CHECK(fs::exists(kRoot / target));
        }
    }
}

TEST_CASE("screen images do not leak local paths", "[readme][images]") {
    std::size_t images = 0;
    for (const auto& lang : {"en", "ru"})
        for (const auto& entry : fs::directory_iterator{kRoot / "docs" / "images" / lang}) {
            if (entry.path().extension() != ".svg") continue;
            ++images;
            const auto text = read(entry.path());
            for (const char* leak : {"/private/", "/Users/", "/home/", "/tmp/", ":\\\\", ":\\"}) {
                INFO(entry.path().filename().string() << " contains " << leak);
                CHECK(text.find(leak) == std::string::npos);
            }
            CHECK(text.find(SIGNALS_SOURCE_DIR) == std::string::npos);
        }
    CHECK(images == 38);
}

TEST_CASE("shell scripts keep LF line endings in every checkout", "[readme][packaging]") {
    bool lf = false;
    for (const auto& line : lines(read(kRoot / ".gitattributes"))) lf = lf || line == "*.sh text eol=lf";
    CHECK(lf);
}
