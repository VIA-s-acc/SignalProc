#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "commands/cli.hpp"
#include "commands/cli_docs.hpp"

using signals::cli::CliEntry;
using signals::cli::ShellKind;

namespace {

std::string token(ShellKind shell, const std::string& name) {
    if (shell != ShellKind::fish) return name;
    if (name.starts_with("--")) return "-l " + name.substr(2);
    return "-s " + name.substr(1);
}

const signals::cli::CliOption* find_option(const std::vector<CliEntry>& entries, std::vector<std::string> path, std::string_view name) {
    for (const auto& e : entries)
        if (e.path == path)
            for (const auto& o : e.options)
                for (const auto& n : o.names)
                    if (n == name) return &o;
    return nullptr;
}

bool have(const char* program) {
    return std::system((std::string{"command -v "} + program + " >/dev/null 2>&1").c_str()) == 0;
}

std::filesystem::path unique_temp(const std::string& name) {
    std::random_device seed;
    return std::filesystem::temp_directory_path() / (name + "-" + std::to_string(seed()) + std::to_string(seed()));
}

bool shell_accepts(const char* program, const std::string& script) {
    const auto file = unique_temp(std::string{"signals-completion-"} + program);
    std::ofstream{file} << script;
    return std::system((std::string{program} + " -n '" + file.string() + "' >/dev/null 2>&1").c_str()) == 0;
}

}  // namespace

TEST_CASE("cli entries cover the whole command tree", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    REQUIRE(entries.size() > 15);
    CHECK(entries.front().path.empty());
    bool learn_check = false, filter_info = false;
    for (const auto& e : entries) {
        learn_check = learn_check || e.path == std::vector<std::string>{"learn", "check"};
        filter_info = filter_info || e.path == std::vector<std::string>{"filter", "info"};
        for (const auto& o : e.options)
            for (const auto& n : o.names) CHECK(n != "--help");
    }
    CHECK(learn_check);
    CHECK(filter_info);
}

TEST_CASE("cli entries read the choices of a member check", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    const auto* theme = find_option(entries, {"tui"}, "--theme");
    REQUIRE(theme);
    CHECK(theme->choices == std::vector<std::string>{"dark", "light"});
    const auto* rule = find_option(entries, {"denoise"}, "--rule");
    REQUIRE(rule);
    CHECK(rule->choices == std::vector<std::string>{"bayes", "visu", "sure", "minimax"});
}

TEST_CASE("completion scripts name every subcommand and option", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    for (auto shell : {ShellKind::bash, ShellKind::zsh, ShellKind::fish}) {
        const auto script = signals::cli::completion_script(entries, shell);
        for (const auto& e : entries) {
            for (const auto& word : e.path) CHECK(script.find(word) != std::string::npos);
            for (const auto& o : e.options)
                for (const auto& n : o.names) CHECK(script.find(token(shell, n)) != std::string::npos);
        }
    }
}

TEST_CASE("completion scripts offer the choices of an option", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    for (auto shell : {ShellKind::bash, ShellKind::zsh, ShellKind::fish}) {
        const auto script = signals::cli::completion_script(entries, shell);
        CHECK(script.find("dark light") != std::string::npos);
        CHECK(script.find("bayes visu sure minimax") != std::string::npos);
    }
}

TEST_CASE("bash, zsh and fish accept the generated scripts", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    REQUIRE(have("bash"));
    CHECK(shell_accepts("bash", signals::cli::completion_script(entries, ShellKind::bash)));
    if (have("zsh")) CHECK(shell_accepts("zsh", signals::cli::completion_script(entries, ShellKind::zsh)));
    if (have("fish")) CHECK(shell_accepts("fish", signals::cli::completion_script(entries, ShellKind::fish)));
}

TEST_CASE("completion scripts quote descriptions with apostrophes", "[cli-docs]") {
    std::vector<CliEntry> entries{{.path = {},
                                   .description = "root",
                                   .options = {{.names = {"--odd"}, .value = "TEXT", .description = "it's a \"test\" $HOME `x`", .choices = {}}},
                                   .positionals = {}}};
    REQUIRE(have("bash"));
    CHECK(shell_accepts("bash", signals::cli::completion_script(entries, ShellKind::bash)));
    if (have("zsh")) CHECK(shell_accepts("zsh", signals::cli::completion_script(entries, ShellKind::zsh)));
    if (have("fish")) CHECK(shell_accepts("fish", signals::cli::completion_script(entries, ShellKind::fish)));
}

TEST_CASE("signals completions bash prints the script", "[cli-docs]") {
    std::ostringstream out, err;
    std::array<std::string_view, 3> args{"signals", "completions", "bash"};
    CHECK(signals::cli::run_cli(args, out, err) == 0);
    CHECK(out.str().find("complete -F _signals signals") != std::string::npos);
}

TEST_CASE("signals completions rejects an unknown shell", "[cli-docs]") {
    std::ostringstream out, err;
    std::array<std::string_view, 3> args{"signals", "completions", "powershell"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK_FALSE(err.str().empty());
}

namespace {

std::string roff_dashes(const std::string& name) {
    std::string out;
    for (char c : name) out += c == '-' ? std::string{"\\-"} : std::string(1, c);
    return out;
}

}  // namespace

TEST_CASE("man page has every subcommand and option", "[cli-docs]") {
    const auto entries = signals::cli::cli_entries();
    const auto page = signals::cli::man_page(entries, "0.0.0");
    CHECK(page.starts_with(".TH SIGNALS 1"));
    for (const char* section : {".SH NAME", ".SH SYNOPSIS", ".SH DESCRIPTION", ".SH OPTIONS", ".SH COMMANDS", ".SH ENVIRONMENT", ".SH FILES",
                                ".SH \"EXIT STATUS\"", ".SH \"SEE ALSO\""})
        CHECK(page.find(section) != std::string::npos);
    for (const auto& e : entries) {
        if (!e.path.empty()) {
            std::string title = "signals";
            for (const auto& w : e.path) title += " " + w;
            CHECK(page.find(".SS \"" + title + "\"") != std::string::npos);
        }
        for (const auto& o : e.options)
            for (const auto& n : o.names) CHECK(page.find(roff_dashes(n)) != std::string::npos);
    }
}

TEST_CASE("man page lists the environment and files", "[cli-docs]") {
    const auto page = signals::cli::man_page(signals::cli::cli_entries(), "0.0.0");
    for (const char* name : {"SIGNALS_LANG", "XDG_CONFIG_HOME", "APPDATA", "NO_COLOR", "EDITOR", "terminal.toml", "session.toml", "learn.toml"})
        CHECK(page.find(name) != std::string::npos);
    CHECK(page.find("0.0.0") != std::string::npos);
}

TEST_CASE("man page escapes roff control characters", "[cli-docs]") {
    std::vector<CliEntry> entries{{.path = {},
                                   .description = "root",
                                   .options = {{.names = {"--dot"}, .value = {}, .description = ".starts with a dot", .choices = {}},
                                               {.names = {"--odd"}, .value = "TEXT", .description = "'quote \\ backslash", .choices = {}}},
                                   .positionals = {}}};
    const auto page = signals::cli::man_page(entries, "0.0.0");
    std::istringstream lines{page};
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with(".") || line.starts_with("'")) {
            CHECK(line.find("starts with a dot") == std::string::npos);
            CHECK(line.find("quote") == std::string::npos);
        }
    }
    CHECK(page.find("\\e backslash") != std::string::npos);
}

TEST_CASE("mandoc or groff accept the man page", "[cli-docs]") {
    const auto file = unique_temp("signals-man") += ".1";
    std::ofstream{file} << signals::cli::man_page(signals::cli::cli_entries(), "0.0.0");
    if (have("mandoc")) {
        CHECK(std::system(("mandoc -T lint -W warning '" + file.string() + "' >/dev/null 2>&1").c_str()) == 0);
    } else if (have("groff")) {
        CHECK(std::system(("groff -man -z -ww '" + file.string() + "' 2>&1 | grep -q . && exit 1 || exit 0").c_str()) == 0);
    } else {
        SKIP("neither mandoc nor groff is installed");
    }
}

TEST_CASE("signals man prints the page", "[cli-docs]") {
    std::ostringstream out, err;
    std::array<std::string_view, 2> args{"signals", "man"};
    CHECK(signals::cli::run_cli(args, out, err) == 0);
    CHECK(out.str().starts_with(".TH SIGNALS 1"));
}
