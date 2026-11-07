#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/command.hpp"
#include "i18n/i18n.hpp"
#include "io/wav.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/layout.hpp"
#include "pipeline/docs.hpp"
#include "pipeline/sig.hpp"

namespace signals::cli {
namespace {

std::atomic<core::CancelToken*> g_cancel{nullptr};

extern "C" void cancel_on_interrupt(int) {
    if (auto* token = g_cancel.load()) token->cancel();
}

int sig_error(std::ostream& err, const std::filesystem::path& file, const pipeline::SigError& e) {
    std::string where = file.string();
    if (e.line > 0) where += ":" + std::to_string(e.line);
    if (e.column > 0) where += ":" + std::to_string(e.column);
    err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", where + ": " + e.message)) << '\n';
    return 1;
}

std::optional<pipeline::PipelineDoc> load(const std::filesystem::path& file, std::ostream& err) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        report_error(err, {core::Errc::io_error, fmt::format(fmt::runtime(tr("pipeline.cli.cannot_read")), fmt::arg("path", file.string()))});
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    auto doc = pipeline::parse_sig(text);
    if (!doc) {
        sig_error(err, file, doc.error());
        return std::nullopt;
    }
    return std::move(*doc);
}

nlohmann::json summary(const pipeline::Value& v) {
    using namespace pipeline;
    nlohmann::json j{{"kind", std::string{to_string(type_of(v))}}};
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) {
        double energy = 0, peak = 0;
        for (const auto& c : a->channels)
            for (const double x : c) {
                energy += x * x;
                peak = std::max(peak, std::abs(x));
            }
        j["channels"] = a->channels.size();
        j["length"] = a->frames();
        j["rate"] = a->sample_rate;
        j["energy"] = energy;
        j["peak"] = peak;
    } else if (const auto* c = std::get_if<Coeffs>(&v)) {
        std::size_t total = 0;
        for (const auto& ch : c->channels)
            for (const auto& b : ch.bands) total += b.size();
        j["channels"] = c->channels.size();
        j["levels"] = c->levels();
        j["length"] = total;
        j["wavelet"] = c->wavelet;
    } else if (const auto* d = std::get_if<double>(&v)) {
        j["value"] = *d;
    } else if (const auto* r = std::get_if<Report>(&v)) {
        for (const auto& [name, value] : r->fields)
            std::visit([&, &key = name](const auto& x) { j["fields"][key] = x; }, value);
    }
    return j;
}

core::Result<void> save_probe(const std::filesystem::path& dir, const std::string& name, const pipeline::Value& v) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) {
        auto copy = *a;
        copy.sample_rate = std::max(1.0, std::round(copy.sample_rate));
        io::WavWriteOptions o;
        o.format = io::SampleFormat::float32;
        o.dither = false;
        auto written = io::write_wav(dir / (name + ".wav"), copy, o);
        if (!written) return std::unexpected{written.error()};
        return {};
    }
    nlohmann::json j = summary(v);
    if (const auto* c = std::get_if<pipeline::Coeffs>(&v)) {
        j["channels"] = nlohmann::json::array();
        for (const auto& ch : c->channels) j["channels"].push_back(ch.bands);
    }
    std::ofstream out{dir / (name + ".json")};
    out << j.dump(2) << '\n';
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + (dir / (name + ".json")).string());
    return {};
}

std::string size_text(std::size_t bytes) {
    if (bytes == 0) return "—";
    if (bytes < 1024) return fmt::format("{} B", bytes);
    if (bytes < 1024 * 1024) return fmt::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

std::string state_mark(pipeline::NodeState s) {
    switch (s) {
    case pipeline::NodeState::done: return "✓";
    case pipeline::NodeState::cached: return "=";
    case pipeline::NodeState::failed: return "✗";
    case pipeline::NodeState::bypassed: return "–";
    default: return "·";
    }
}

class RunCommand final : public Command {
public:
    explicit RunCommand(CLI::App& app) : sub_{app.add_subcommand("run", tr("cli.run.description"))} {
        sub_->add_option("file", file_, tr("cli.run.file"))->required();
        sub_->add_option("--set", sets_, tr("cli.run.set"));
        sub_->add_option("-o,--output", output_, tr("cli.run.output"));
        sub_->add_option("--probe-dir", probe_dir_, tr("cli.run.probe_dir"));
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const std::filesystem::path file{file_};
        auto doc = load(file, err);
        if (!doc) return 1;
        for (const auto& s : sets_)
            if (auto ok = pipeline::apply_set(*doc, s); !ok) return report_error(err, ok.error()), 1;
        if (!output_.empty()) {
            const auto target = std::find_if(doc->nodes.begin(), doc->nodes.end(), [](const auto& n) { return n.type == "wav_out"; });
            if (target == doc->nodes.end()) return report_error(err, {core::Errc::invalid_argument, tr("pipeline.cli.no_output")}), 1;
            target->params["path"] = std::filesystem::absolute(output_).string();
        }
        auto graph = pipeline::build_graph(*doc, std::filesystem::absolute(file).parent_path());
        if (!graph) return sig_error(err, file, graph.error());
        for (const auto& n : graph->nodes)
            if (n.type->id == "listen") err << fmt::format(fmt::runtime(tr("pipeline.cli.listen_skipped")), fmt::arg("node", n.id)) << '\n';

        pipeline::ResultCache cache{0};
        core::CancelToken cancel;
        g_cancel.store(&cancel);
        const auto previous = std::signal(SIGINT, cancel_on_interrupt);
        const auto result = pipeline::run_graph(*graph, cache, {}, cancel);
        std::signal(SIGINT, previous);
        g_cancel.store(nullptr);
        if (!result) {
            if (result.error().code == core::Errc::cancelled) {
                err << tr("pipeline.cli.cancelled") << '\n';
                return 130;
            }
            return report_error(err, result.error()), 1;
        }
        if (!probe_dir_.empty())
            for (const auto& [name, value] : result->probes)
                if (auto ok = save_probe(probe_dir_, name, value); !ok) return report_error(err, ok.error()), 1;
        if (json_) print_json(*doc, *graph, *result, out);
        else print_text(*doc, *graph, *result, out);
        return result->checks_passed ? 0 : 3;
    }

private:
    static void print_json(const pipeline::PipelineDoc& doc, const pipeline::Graph& g, const pipeline::RunResult& r, std::ostream& out) {
        nlohmann::json j{{"pipeline", doc.name}, {"checks_passed", r.checks_passed}};
        j["nodes"] = nlohmann::json::array();
        for (std::size_t i = 0; i < r.nodes.size(); ++i) {
            const auto& s = r.nodes[i];
            j["nodes"].push_back({{"id", s.id},
                                  {"type", g.nodes[i].type->id},
                                  {"state", std::string{pipeline::to_string(s.state)}},
                                  {"wall_ms", s.wall_seconds * 1e3},
                                  {"cpu_ms", s.cpu_seconds * 1e3},
                                  {"bytes", s.bytes},
                                  {"message", s.message}});
        }
        j["probes"] = nlohmann::json::object();
        for (const auto& [name, value] : r.probes) j["probes"][name] = summary(value);
        j["reports"] = nlohmann::json::array();
        for (const auto& nr : r.reports) {
            nlohmann::json fields = nlohmann::json::object();
            for (const auto& [name, value] : nr.report.fields)
                std::visit([&, &key = name](const auto& x) { fields[key] = x; }, value);
            j["reports"].push_back({{"node", nr.node}, {"passed", nr.report.passed}, {"fields", fields}});
        }
        out << j.dump(2) << '\n';
    }

    static void print_text(const pipeline::PipelineDoc& doc, const pipeline::Graph& g, const pipeline::RunResult& r, std::ostream& out) {
        out << fmt::format(fmt::runtime(tr("pipeline.cli.header")), fmt::arg("name", doc.name.empty() ? std::string{"—"} : doc.name),
                           fmt::arg("nodes", g.nodes.size()))
            << '\n';
        std::size_t id_width = 0, type_width = 0;
        for (const auto& n : g.nodes) {
            id_width = std::max(id_width, n.id.size());
            type_width = std::max(type_width, n.type->id.size());
        }
        for (const auto i : g.order) {
            const auto& s = r.nodes[i];
            out << fmt::format("  {} {:<{}}  {:<{}}  {:>9.1f} ms  {:>10}", state_mark(s.state), s.id, id_width, g.nodes[i].type->id, type_width,
                               s.wall_seconds * 1e3, size_text(s.bytes))
                << '\n';
        }
        if (!r.probes.empty()) out << tr("pipeline.cli.probes") << '\n';
        for (const auto& [name, value] : r.probes) {
            const auto j = summary(value);
            std::string line = "  " + name + ": " + j["kind"].get<std::string>();
            if (j.contains("length")) line += fmt::format(" · {} × {}", j["channels"].get<std::size_t>(), j["length"].get<std::size_t>());
            if (j.contains("energy")) line += fmt::format(" · energy {:.6g} · peak {:.6g}", j["energy"].get<double>(), j["peak"].get<double>());
            out << line << '\n';
        }
        if (!r.reports.empty()) out << tr("pipeline.cli.reports") << '\n';
        for (const auto& nr : r.reports) {
            std::string fields;
            for (const auto& [name, value] : nr.report.fields)
                fields += (fields.empty() ? "" : " · ") + name + " = " +
                          std::visit([](const auto& x) {
                              if constexpr (std::is_same_v<std::decay_t<decltype(x)>, double>) return fmt::format("{:.6g}", x);
                              else return std::string{x};
                          },
                                     value);
            out << "  " << nr.node << ": " << fields << (nr.report.passed ? "" : " — " + tr("pipeline.cli.failed")) << '\n';
        }
    }

    CLI::App* sub_;
    std::string file_, output_, probe_dir_;
    std::vector<std::string> sets_;
    bool json_ = false;
};

class GraphCommand final : public Command {
public:
    explicit GraphCommand(CLI::App& app) : sub_{app.add_subcommand("graph", tr("cli.graph.description"))} {
        sub_->add_option("file", file_, tr("cli.run.file"))->required();
        sub_->add_flag("--ascii", ascii_, tr("cli.graph.ascii"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const std::filesystem::path file{file_};
        auto doc = load(file, err);
        if (!doc) return 1;
        auto graph = pipeline::build_graph(*doc, std::filesystem::absolute(file).parent_path());
        if (!graph) return sig_error(err, file, graph.error());
        for (const auto& line : pipeline::draw_graph(*graph, ascii_)) out << line << '\n';
        return 0;
    }

private:
    CLI::App* sub_;
    std::string file_;
    bool ascii_ = false;
};

class BlocksCommand final : public Command {
public:
    explicit BlocksCommand(CLI::App& app) : sub_{app.add_subcommand("blocks", tr("cli.blocks.description"))} {
        sub_->add_flag("--markdown", markdown_, tr("cli.blocks.markdown"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream&) override {
        out << (markdown_ ? pipeline::blocks_markdown() : pipeline::blocks_text());
        return 0;
    }

private:
    CLI::App* sub_;
    bool markdown_ = false;
};

}  // namespace

void add_pipeline_commands(CLI::App& app, Commands& commands) {
    commands.push_back(std::make_unique<RunCommand>(app));
    commands.push_back(std::make_unique<GraphCommand>(app));
    commands.push_back(std::make_unique<BlocksCommand>(app));
}

}  // namespace signals::cli
