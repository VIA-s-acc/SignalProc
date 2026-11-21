#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#include "pipeline/renames.hpp"

namespace {

namespace fs = std::filesystem;

const fs::path kRoot{SIGNALS_SOURCE_DIR};

std::string bytes(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

bool hand_written(const fs::path& p) {
    const auto rel = p.lexically_relative(kRoot).generic_string();
    const auto name = p.filename().string();
    if (rel.starts_with("tests/fixtures/sig/v1/") || name.ends_with(".v1.sig") || name.ends_with(".v1.toml")) return false;
    if (name == "blocks.md" || name == "blocks.en.md" || name == "keys.md" || name == "keys.en.md") return false;
    const auto ext = p.extension().string();
    return ext == ".md" || ext == ".sig" || ext == ".toml" || ext == ".ipynb" || ext == ".json";
}

std::vector<fs::path> texts() {
    std::vector<fs::path> out{kRoot / "README.md", kRoot / "README.ru.md", kRoot / "docs" / "02-theory.md", kRoot / "docs" / "02-theory.en.md", kRoot / "docs" / "03-design.md"};
    for (const auto& dir : {kRoot / "docs" / "user", kRoot / "examples", kRoot / "resources" / "learn", kRoot / "resources" / "theory", kRoot / "editors" / "vscode" / "snippets",
                            kRoot / "tests" / "fixtures"})
        for (const auto& e : fs::recursive_directory_iterator{dir})
            if (e.is_regular_file() && hand_written(e.path())) out.push_back(e.path());
    out.push_back(kRoot / "editors" / "vscode" / "README.md");
    std::ranges::sort(out);
    return out;
}

std::string without_change_table(std::string text) {
    for (const auto* id : {"{#sig-v2-changes}"}) {
        const auto at = text.find(id);
        if (at == std::string::npos) continue;
        const auto start = text.rfind('\n', at);
        const auto next = text.find("\n#", at);
        text.erase(start == std::string::npos ? 0 : start, (next == std::string::npos ? text.size() : next) - (start == std::string::npos ? 0 : start));
    }
    return text;
}

std::vector<std::regex> patterns_of(const signals::pipeline::Rename& r) {
    using K = signals::pipeline::Rename::Kind;
    const auto b = r.block, f = r.from;
    switch (r.kind) {
    case K::param:
        return {std::regex{"\\b" + b + "\\([^)]*\\b" + f + "\\s*="}, std::regex{"\\b" + b + "\\." + f + "\\b"}};
    case K::choice:
        return {std::regex{"\\b" + b + "\\([^)]*\\b" + r.param + "\\s*=\\s*" + f + "\\b"}};
    case K::field:
        return {std::regex{"\\b[a-d]\\." + f + "\\b"}, std::regex{"field\\s*=\\s*\"" + f + "\""}, std::regex{"`" + f + "`"}};
    case K::block:
    case K::port:
        break;
    }
    return {std::regex{"\\b" + f + "\\("}};
}

}  // namespace

TEST_CASE("no old names remain in hand-written texts", "[old-names]") {
    const auto files = texts();
    REQUIRE(files.size() > 300);
    for (const auto& file : files) {
        const auto text = without_change_table(bytes(file));
        for (const auto& r : signals::pipeline::v1_renames())
            for (const auto& re : patterns_of(r)) {
                std::smatch m;
                if (!std::regex_search(text, m, re)) continue;
                const auto line = 1 + std::count(text.begin(), text.begin() + m.position(0), '\n');
                FAIL_CHECK(file.lexically_relative(kRoot).generic_string() << ":" << line << ": " << r.block << " " << r.from << " → " << r.to << " in «" << m.str(0) << "»");
            }
    }
}

TEST_CASE("the change table lists every rename in both languages", "[old-names]") {
    for (const auto* doc : {"sig-format.md", "sig-format.en.md"}) {
        const auto text = bytes(kRoot / "docs" / "user" / doc);
        const auto at = text.find("{#sig-v2-changes}");
        REQUIRE(at != std::string::npos);
        const auto table = text.substr(at, text.find("\n#", at) - at);
        for (const auto& r : signals::pipeline::v1_renames()) {
            INFO(doc << ": " << r.block << " " << r.from << " → " << r.to);
            const auto row = table.find("| `" + r.block + "` |");
            CHECK(row != std::string::npos);
            CHECK(table.find(r.from) != std::string::npos);
            CHECK(table.find(r.to) != std::string::npos);
        }
    }
}
