#include "commands/cli.hpp"

#include <algorithm>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/i18n.hpp"
#include "core/version.hpp"

namespace signals::cli {
namespace {

int run_check(bool json, std::ostream& out) {
    std::vector<core::CheckResult> results;
    for (CheckSuite suite : check_suites()) {
        auto part = suite();
        results.insert(results.end(), part.begin(), part.end());
    }
    const auto failed = std::ranges::count_if(results, [](const auto& r) { return !r.passed(); });
    const auto passed = static_cast<std::ptrdiff_t>(results.size()) - failed;
    if (json) {
        nlohmann::json j;
        j["checks"] = nlohmann::json::array();
        for (const auto& r : results) {
            j["checks"].push_back({{"id", r.id}, {"subject", r.subject}, {"value", r.value},
                                   {"tolerance", r.tolerance}, {"passed", r.passed()}});
        }
        j["passed"] = passed;
        j["failed"] = failed;
        out << j.dump(2) << '\n';
    } else {
        for (const auto& r : results) {
            if (r.passed()) continue;
            out << fmt::format(fmt::runtime(tr("check.failed_line")), fmt::arg("id", r.id),
                               fmt::arg("subject", r.subject), fmt::arg("value", r.value),
                               fmt::arg("tolerance", r.tolerance))
                << '\n';
        }
        out << fmt::format(fmt::runtime(tr("check.summary")), fmt::arg("passed", passed),
                           fmt::arg("failed", failed))
            << '\n';
    }
    return failed == 0 ? 0 : 1;
}

}  // namespace

std::vector<CheckSuite>& check_suites() {
    static std::vector<CheckSuite> suites;
    return suites;
}

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    CLI::App app{tr("cli.description"), "signals"};
    bool show_version = false;
    app.add_flag("--version", show_version);
    bool check_json = false;
    auto* check = app.add_subcommand("check", tr("cli.check.description"));
    check->add_flag("--json", check_json, tr("cli.json.description"));

    std::vector<std::string> rest;
    for (std::size_t i = args.size(); i > 1; --i) rest.emplace_back(args[i - 1]);
    try {
        app.parse(rest);
    } catch (const CLI::ParseError& e) {
        err << fmt::format(fmt::runtime(tr("cli.unknown_command")), fmt::arg("details", e.what())) << '\n';
        return 2;
    }

    if (show_version) {
        out << fmt::format(fmt::runtime(tr("cli.version")), fmt::arg("version", core::version())) << '\n';
        return 0;
    }
    if (check->parsed()) return run_check(check_json, out);
    out << app.help();
    return 0;
}

}  // namespace signals::cli
