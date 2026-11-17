#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/migrate_files.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/sig.hpp"

namespace {

namespace fs = std::filesystem;

const fs::path kRoot{SIGNALS_SOURCE_DIR};

std::string bytes(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Fence {
    std::string lang, body;
    int line = 0;
};

std::vector<Fence> fences(std::string_view text) {
    std::vector<Fence> out;
    std::optional<Fence> open;
    int number = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto end = text.find('\n', at);
        const auto line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        ++number;
        const auto trimmed = line.substr(std::min(line.find_first_not_of(' '), line.size()));
        if (trimmed.starts_with("```")) {
            if (open) {
                out.push_back(std::move(*open));
                open.reset();
            } else {
                open = Fence{std::string{trimmed.substr(3)}, {}, number};
            }
        } else if (open) {
            open->body += std::string{line} + "\n";
        }
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return out;
}

std::string notebook_text(const fs::path& file) {
    std::string out;
    const auto notebook = nlohmann::json::parse(bytes(file));
    for (const auto& cell : notebook.at("cells"))
        for (const auto& line : cell.at("source")) out += line.get<std::string>();
    return out;
}

std::vector<fs::path> texts() {
    std::vector<fs::path> out{kRoot / "README.md", kRoot / "README.ru.md", kRoot / "docs" / "03-design.md", kRoot / "editors" / "vscode" / "README.md"};
    for (const auto& dir : {kRoot / "docs" / "user", kRoot / "examples", kRoot / "resources" / "learn" / "course", kRoot / "resources" / "theory"})
        for (const auto& e : fs::recursive_directory_iterator{dir})
            if (e.path().extension() == ".md" || e.path().extension() == ".ipynb") out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

}  // namespace

TEST_CASE("graph snippets in texts parse as the current version", "[doc-snippets]") {
    int checked = 0;
    for (const auto& file : texts()) {
        const auto text = file.extension() == ".ipynb" ? notebook_text(file) : bytes(file);
        for (const auto& f : fences(text)) {
            const bool headed = f.body.starts_with("pipeline \"");
            if (!headed && f.lang != "sig") continue;
            INFO(file.lexically_relative(kRoot).generic_string() << ":" << f.line);
            const auto loaded = signals::pipeline::load_sig(headed ? f.body : "pipeline \"snippet\" v2\n" + f.body);
            REQUIRE(loaded);
            CHECK(loaded->from_version == signals::pipeline::kSigVersion);
            ++checked;
        }
    }
    CHECK(checked >= 4);
}

TEST_CASE("the readme copy of first_steps equals the example", "[doc-snippets]") {
    const auto example = bytes(kRoot / "examples" / "first_steps.sig");
    for (const auto* readme : {"README.md", "README.ru.md"}) {
        INFO(readme);
        bool found = false;
        for (const auto& f : fences(bytes(kRoot / readme)))
            if (f.body.starts_with("pipeline \"first_steps\"")) found = f.body == example;
        CHECK(found);
    }
}

TEST_CASE("every graph in the repository is in the current format", "[doc-snippets]") {
    const std::vector<fs::path> paths{kRoot / "examples", kRoot / "resources" / "learn", kRoot / "tests" / "fixtures" / "learn", kRoot / "tests" / "fixtures" / "noise"};
    const auto reports = signals::cli::migrate_paths(paths, false, false);
    CHECK(reports.size() >= 178);
    for (const auto& r : reports) {
        INFO(r.file.string());
        CHECK_FALSE(r.error);
        CHECK(r.from == signals::pipeline::kSigVersion);
    }
}
