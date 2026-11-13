#include "commands/cli.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/cli_docs.hpp"
#include "commands/cli_tree.hpp"
#include "commands/command.hpp"
#include "commands/suites.hpp"
#include "core/design/families.hpp"
#include "core/design/split.hpp"
#include "core/version.hpp"
#include "core/wavelet/regularity.hpp"
#include "i18n/i18n.hpp"

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

std::string join(const std::vector<double>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + fmt::format("{:.10g}", v[i]);
    return s;
}

std::string yes_no(bool b) { return tr(b ? "yes" : "no"); }

int run_filter_info(const std::string& name, bool json, std::ostream& out, std::ostream& err) {
    const auto bank = core::wavelet(name);
    const auto info = core::wavelet_info(name);
    if (!bank) return report_error(err, bank.error());
    if (!info) return report_error(err, info.error());
    const double tol = base_tolerance(info->generated, name == "dmey");
    const auto pr = core::check_pr(*bank, tol);
    const bool orthogonal = core::check_orthogonality(*bank, tol).passed();
    const bool linear_phase = core::is_linear_phase(bank->h0, 1e-9) && core::is_linear_phase(bank->f0, 1e-9);
    const auto regularity = core::wavelet_regularity(name);
    auto as_json = [](const auto& v) { return v ? nlohmann::json(*v) : nlohmann::json(nullptr); };
    auto as_text = [](const auto& v, const char* format) { return v ? fmt::format(fmt::runtime(format), *v) : std::string{"—"}; };
    const double pr_error = std::max(pr.alias.value, pr.distortion.value);
    if (json) {
        nlohmann::json j{{"name", bank->name}, {"family", info->family}, {"length", bank->length()},
                         {"h0", bank->h0.coeffs}, {"h1", bank->h1.coeffs}, {"f0", bank->f0.coeffs}, {"f1", bank->f1.coeffs},
                         {"orthogonal", orthogonal}, {"linear_phase", linear_phase}, {"zeros_h0", as_json(regularity->zeros_h0)},
                         {"zeros_f0", as_json(regularity->zeros_f0)}, {"sobolev_h0", as_json(regularity->sobolev_h0)},
                         {"sobolev_f0", as_json(regularity->sobolev_f0)}, {"pr_error", pr_error}};
        j["delay"] = pr.delay ? nlohmann::json(*pr.delay) : nlohmann::json(nullptr);
        out << j.dump(2) << '\n';
        return 0;
    }
    out << fmt::format(fmt::runtime(tr("filter.header")), fmt::arg("name", bank->name), fmt::arg("family", info->family),
                       fmt::arg("length", bank->length())) << '\n';
    out << fmt::format(fmt::runtime(tr("filter.delay")), fmt::arg("delay", pr.delay ? std::to_string(*pr.delay) : "—"),
                       fmt::arg("pr_error", fmt::format("{:.1e}", pr_error))) << '\n';
    out << fmt::format(fmt::runtime(tr("filter.properties")), fmt::arg("orthogonal", yes_no(orthogonal)),
                       fmt::arg("linear_phase", yes_no(linear_phase))) << '\n';
    out << fmt::format(fmt::runtime(tr("filter.zeros")), fmt::arg("zeros_h0", as_text(regularity->zeros_h0, "{}")),
                       fmt::arg("zeros_f0", as_text(regularity->zeros_f0, "{}"))) << '\n';
    out << fmt::format(fmt::runtime(tr("filter.sobolev")), fmt::arg("sobolev_h0", as_text(regularity->sobolev_h0, "{:.3f}")),
                       fmt::arg("sobolev_f0", as_text(regularity->sobolev_f0, "{:.3f}"))) << '\n';
    for (const auto& [role, filter] : {std::pair{"h0", &bank->h0}, std::pair{"h1", &bank->h1}, std::pair{"f0", &bank->f0}, std::pair{"f1", &bank->f1}})
        out << fmt::format(fmt::runtime(tr("filter.coeffs")), fmt::arg("role", role), fmt::arg("values", join(filter->coeffs))) << '\n';
    return 0;
}

std::string_view kind_name(core::SplitKind k) {
    switch (k) {
        case core::SplitKind::orthogonal: return "orthogonal";
        case core::SplitKind::linear_phase: return "linear_phase";
        case core::SplitKind::other: return "other";
    }
    return "other";
}

int run_design(int p, bool json, std::ostream& out, std::ostream& err) {
    if (p < 1 || p > 6) return report_error(err, {core::Errc::invalid_argument, "--p must be in [1, 6]"});
    const auto f = core::factorise_maxflat(p);
    nlohmann::json splits = nlohmann::json::array();
    int wavelets = 0;
    std::size_t skipped = 0;
    std::vector<std::string> rows;
    for (const auto& s : core::enumerate_splits(f)) {
        const auto bank = core::bank_from_split(f, s, "split");
        if (!bank) {
            ++skipped;
            continue;
        }
        const auto info = core::classify(*bank);
        wavelets += info.wavelet ? 1 : 0;
        std::vector<bool> groups(s.group_in_h0.begin(), s.group_in_h0.end());
        splits.push_back({{"zeros_h0", s.zeros_h0}, {"groups_in_h0", groups}, {"kind", kind_name(info.kind)},
                          {"sobolev_h0", info.sobolev_h0}, {"sobolev_f0", info.sobolev_f0}, {"wavelet", info.wavelet},
                          {"length_h0", core::trim(bank->h0, 0.0).coeffs.size()},
                          {"length_f0", core::trim(bank->f0, 0.0).coeffs.size()}});
        std::string g;
        for (bool b : groups) g += b ? '1' : '0';
        rows.push_back(fmt::format(fmt::runtime(tr("design.row")), fmt::arg("zeros", s.zeros_h0), fmt::arg("groups", g.empty() ? "—" : g),
                                   fmt::arg("kind", tr("design.kind." + std::string{kind_name(info.kind)})), fmt::arg("sh", info.sobolev_h0), fmt::arg("sf", info.sobolev_f0),
                                   fmt::arg("verdict", tr(info.wavelet ? "design.wavelet" : "design.not_wavelet"))));
    }
    if (json) {
        out << nlohmann::json{{"p", p}, {"splits", splits}, {"wavelets", wavelets}, {"skipped", skipped}}.dump(2) << '\n';
        return 0;
    }
    out << fmt::format(fmt::runtime(tr("design.header")), fmt::arg("p", p), fmt::arg("count", tr_count("unit.split", static_cast<long long>(rows.size()))), fmt::arg("wavelets", wavelets)) << '\n';
    for (const auto& r : rows) out << r << '\n';
    if (skipped > 0) out << fmt::format(fmt::runtime(tr("design.skipped")), fmt::arg("count", skipped)) << '\n';
    return 0;
}

}  // namespace

std::vector<CheckSuite>& check_suites() {
    static std::vector<CheckSuite> suites{&core_suite};
    return suites;
}

namespace {

struct CliBindings {
    std::string lang_option;
    bool show_version = false;
    bool check_json = false;
    std::string filter_name;
    bool filter_json = false;
    int design_p = 2;
    bool design_json = false;
    std::string tui_file;
    std::string tui_theme;
    std::string completions_shell;
    Commands commands;
    CLI::App* man = nullptr;
    CLI::App* check = nullptr;
    CLI::App* filter_info = nullptr;
    CLI::App* design = nullptr;
    CLI::App* tui = nullptr;
    CLI::App* completions = nullptr;
};

void build_cli(CLI::App& app, CliBindings& b) {
    app.add_option("--lang", b.lang_option, tr("cli.lang"))->check(CLI::IsMember({"ru", "en"}));
    app.add_flag("--version", b.show_version, tr("cli.version.description"));
    b.check = app.add_subcommand("check", tr("cli.check.description"));
    b.check->add_flag("--json", b.check_json, tr("cli.json.description"));
    auto* filter = app.add_subcommand("filter", tr("cli.filter.description"));
    b.filter_info = filter->add_subcommand("info", tr("cli.filter.info.description"));
    b.filter_info->add_option("name", b.filter_name, tr("cli.filter.name"))->required();
    b.filter_info->add_flag("--json", b.filter_json, tr("cli.json.description"));
    filter->require_subcommand(1);
    b.design = app.add_subcommand("design", tr("cli.design.description"));
    b.design->add_option("--p", b.design_p, tr("cli.design.p"))->required();
    b.design->add_flag("--json", b.design_json, tr("cli.json.description"));
    b.tui = app.add_subcommand("tui", tr("cli.tui.description"));
    b.tui->add_option("file", b.tui_file, tr("cli.tui.file"));
    b.tui->add_option("--theme", b.tui_theme, tr("cli.tui.theme"))->check(CLI::IsMember({"dark", "light"}));
    add_audio_commands(app, b.commands);
    add_analysis_commands(app, *filter, b.commands);
    add_pipeline_commands(app, b.commands);
    add_learn_commands(app, b.commands);
    b.completions = app.add_subcommand("completions", tr("cli.completions.description"));
    b.completions->add_option("shell", b.completions_shell, tr("cli.completions.shell"))->required()->check(CLI::IsMember({"bash", "zsh", "fish"}));
    b.man = app.add_subcommand("man", tr("cli.man.command"));
}

ShellKind shell_kind(std::string_view name) {
    if (name == "zsh") return ShellKind::zsh;
    if (name == "fish") return ShellKind::fish;
    return ShellKind::bash;
}

int run_cli_unguarded(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
                      std::optional<TuiRequest>* tui_request) {
    CLI::App app{tr("cli.description"), "signals"};
    CliBindings b;
    build_cli(app, b);

    std::vector<std::string> rest;
    for (std::size_t i = args.size(); i > 1; --i) rest.emplace_back(args[i - 1]);
    try {
        app.parse(rest);
    } catch (const CLI::CallForHelp& e) {
        return app.exit(e, out, err);
    } catch (const CLI::CallForAllHelp& e) {
        return app.exit(e, out, err);
    } catch (const CLI::ParseError& e) {
        err << fmt::format(fmt::runtime(tr("cli.unknown_command")), fmt::arg("details", e.what())) << '\n';
        return 2;
    }

    if (b.show_version) {
        out << fmt::format(fmt::runtime(tr("cli.version")), fmt::arg("version", core::version())) << '\n';
        return 0;
    }
    if (b.check->parsed()) return run_check(b.check_json, out);
    if (b.filter_info->parsed()) return run_filter_info(b.filter_name, b.filter_json, out, err);
    if (b.design->parsed()) return run_design(b.design_p, b.design_json, out, err);
    if (b.tui->parsed()) {
        if (!tui_request) return report_error(err, {core::Errc::invalid_argument, tr("tui.unavailable")});
        *tui_request = TuiRequest{b.tui_file.empty() ? std::nullopt : std::optional<std::filesystem::path>{b.tui_file}, b.tui_theme, std::nullopt};
        return 0;
    }
    if (b.completions->parsed()) {
        out << completion_script(cli_entries(app), shell_kind(b.completions_shell));
        return 0;
    }
    if (b.man->parsed()) {
        out << man_page(cli_entries(app), core::version());
        return 0;
    }
    for (const auto& command : b.commands)
        if (command->parsed()) return command->run(out, err);
    out << app.help();
    return 0;
}

}  // namespace

void with_cli_tree(const std::function<void(const CLI::App&)>& visit) {
    CLI::App app{tr("cli.description"), "signals"};
    CliBindings b;
    build_cli(app, b);
    visit(app);
}

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
            std::optional<TuiRequest>* tui) {
    std::vector<std::string_view> rest;
    std::optional<std::string_view> lang_text;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0 && args[i] == "--lang" && i + 1 < args.size()) lang_text = args[++i];
        else if (i > 0 && args[i].starts_with("--lang=")) lang_text = args[i].substr(7);
        else rest.push_back(args[i]);
    }
    std::optional<Language> chosen;
    if (lang_text) {
        chosen = parse_language(*lang_text);
        if (!chosen) {
            err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", fmt::format(fmt::runtime(tr("cli.bad_lang")), fmt::arg("value", *lang_text))))
                << '\n';
            return 2;
        }
    } else if (const char* env = std::getenv("SIGNALS_LANG")) {
        chosen = parse_language(env);
    }
    const LanguageScope scope{chosen.value_or(language())};
    try {
        const int code = run_cli_unguarded(rest, out, err, tui);
        if (tui && *tui && chosen) (*tui)->language = chosen;
        return code;
    } catch (const std::exception& e) {
        const auto message = fmt::format(fmt::runtime(tr("error.unexpected")), fmt::arg("details", e.what()));
        err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", message)) << '\n';
        return 2;
    }
}

}  // namespace signals::cli
