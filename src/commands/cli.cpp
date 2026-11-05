#include "commands/cli.hpp"

#include <algorithm>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/command.hpp"
#include "commands/i18n.hpp"
#include "commands/suites.hpp"
#include "core/design/families.hpp"
#include "core/design/split.hpp"
#include "core/version.hpp"
#include "core/wavelet/regularity.hpp"

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
    out << fmt::format(fmt::runtime(tr("design.header")), fmt::arg("p", p), fmt::arg("count", rows.size()), fmt::arg("wavelets", wavelets)) << '\n';
    for (const auto& r : rows) out << r << '\n';
    if (skipped > 0) out << fmt::format(fmt::runtime(tr("design.skipped")), fmt::arg("count", skipped)) << '\n';
    return 0;
}

}  // namespace

std::vector<CheckSuite>& check_suites() {
    static std::vector<CheckSuite> suites{&core_suite};
    return suites;
}

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    CLI::App app{tr("cli.description"), "signals"};
    bool show_version = false;
    app.add_flag("--version", show_version, tr("cli.version.description"));
    bool check_json = false;
    auto* check = app.add_subcommand("check", tr("cli.check.description"));
    check->add_flag("--json", check_json, tr("cli.json.description"));
    auto* filter = app.add_subcommand("filter", tr("cli.filter.description"));
    auto* filter_info = filter->add_subcommand("info", tr("cli.filter.info.description"));
    std::string filter_name;
    bool filter_json = false;
    filter_info->add_option("name", filter_name, tr("cli.filter.name"))->required();
    filter_info->add_flag("--json", filter_json, tr("cli.json.description"));
    filter->require_subcommand(1);
    auto* design = app.add_subcommand("design", tr("cli.design.description"));
    int design_p = 2;
    bool design_json = false;
    design->add_option("--p", design_p, tr("cli.design.p"))->required();
    design->add_flag("--json", design_json, tr("cli.json.description"));
    Commands commands;
    add_audio_commands(app, commands);
    add_analysis_commands(app, *filter, commands);

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

    if (show_version) {
        out << fmt::format(fmt::runtime(tr("cli.version")), fmt::arg("version", core::version())) << '\n';
        return 0;
    }
    if (check->parsed()) return run_check(check_json, out);
    if (filter_info->parsed()) return run_filter_info(filter_name, filter_json, out, err);
    if (design->parsed()) return run_design(design_p, design_json, out, err);
    for (const auto& command : commands)
        if (command->parsed()) return command->run(out, err);
    out << app.help();
    return 0;
}

}  // namespace signals::cli
