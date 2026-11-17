#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "commands/command.hpp"
#include "commands/migrate_files.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/sig.hpp"

namespace signals::cli {
namespace {

std::string shown(const std::filesystem::path& p) {
    const auto u = p.generic_u8string();
    return {u.begin(), u.end()};
}

class MigrateCommand final : public Command {
public:
    explicit MigrateCommand(CLI::App& app) : sub_{app.add_subcommand("migrate", tr("cli.migrate.description"))} {
        sub_->add_option("paths", paths_, tr("cli.migrate.paths"))->required();
        sub_->add_flag("--write", write_, tr("cli.migrate.write"));
        sub_->add_flag("--no-backup", no_backup_, tr("cli.migrate.no_backup"));
        sub_->add_flag("--check", check_, tr("cli.migrate.check"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        std::vector<std::filesystem::path> paths;
        for (const auto& p : paths_) paths.emplace_back(std::u8string{p.begin(), p.end()});
        const auto reports = migrate_paths(paths, write_, !no_backup_);
        if (reports.empty()) out << tr("cli.migrate.none") << '\n';
        bool failed = false, old = false;
        for (const auto& r : reports) {
            if (r.error) {
                failed = true;
                err << fmt::format(fmt::runtime(tr("cli.migrate.error")), fmt::arg("file", shown(r.file)), fmt::arg("message", *r.error)) << '\n';
                continue;
            }
            if (r.from >= pipeline::kSigVersion) {
                out << fmt::format(fmt::runtime(tr("cli.migrate.current")), fmt::arg("file", shown(r.file)), fmt::arg("to", pipeline::kSigVersion)) << '\n';
                continue;
            }
            old = old || !r.written;
            out << fmt::format(fmt::runtime(tr(r.written ? "cli.migrate.written" : "cli.migrate.old")), fmt::arg("file", shown(r.file)), fmt::arg("from", r.from),
                               fmt::arg("to", pipeline::kSigVersion))
                << '\n';
            for (const auto& note : r.notes) out << "  " << note << '\n';
        }
        return failed || (check_ && old) ? 1 : 0;
    }

private:
    CLI::App* sub_;
    std::vector<std::string> paths_;
    bool write_ = false, no_backup_ = false, check_ = false;
};

}  // namespace

void add_migrate_commands(CLI::App& app, Commands& commands) { commands.push_back(std::make_unique<MigrateCommand>(app)); }

}  // namespace signals::cli
