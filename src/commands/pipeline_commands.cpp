#include <algorithm>
#include <array>
#include <thread>
#include <optional>
#include <chrono>
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
#include "commands/run_report.hpp"
#include "commands/watch.hpp"
#include "io/temp_file.hpp"
#include "pipeline/sweep.hpp"
#include "svg/svg.hpp"
#include "i18n/i18n.hpp"
#include "io/wav.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/layout.hpp"
#include "pipeline/docs.hpp"
#include "pipeline/sig.hpp"
#include "theory/cards.hpp"
#include "theory/pipeline_cards.hpp"
#include "theory/sections.hpp"

namespace signals::cli {
namespace {

std::atomic<core::CancelToken*> g_cancel{nullptr};

std::atomic<bool> g_stop{false};

extern "C" void stop_on_interrupt(int) {
    g_stop.store(true);
    if (auto* token = g_cancel.load()) token->cancel();
}

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

constexpr std::size_t kListedFrames = 64;

nlohmann::json shape_json(const std::vector<pipeline::OutputShape>& shapes) {
    auto out = nlohmann::json::array();
    for (const auto& s : shapes)
        out.push_back({{"kind", std::string{pipeline::to_string(s.type)}}, {"channels", s.channels}, {"frames", s.frames}, {"rate", s.rate}});
    return out;
}

std::size_t columns(const std::string& s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string shape_text(const std::vector<pipeline::OutputShape>& shapes) {
    std::string out;
    for (const auto& s : shapes) {
        const bool sized = s.type == pipeline::PortType::signal || s.type == pipeline::PortType::coeffs;
        out += (out.empty() ? "" : ", ") + (sized ? fmt::format("{}×{}", s.channels, s.frames) : std::string{pipeline::to_string(s.type)});
    }
    return out;
}

nlohmann::json summary(const pipeline::Value& v, std::optional<std::size_t> listed = std::nullopt) {
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
        if (!listed && a->frames() <= kListedFrames) j["values"] = a->channels;
        if (listed && *listed > 0) {
            auto values = nlohmann::json::array();
            for (const auto& c : a->channels) values.push_back(std::vector<double>(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(std::min(*listed, c.size()))));
            j["values"] = std::move(values);
            j["values_cut"] = *listed < a->frames();
        }
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
        sub_->add_option("--report", report_, tr("cli.run.report"));
        sub_->add_option("--values", values_, tr("cli.run.values"));
        sub_->add_flag("--watch", watch_, tr("cli.run.watch"));
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        if (!watch_) return run_once(out, err);
        const std::filesystem::path file{file_};
        g_stop.store(false);
        const auto previous = std::signal(SIGINT, stop_on_interrupt);
        const auto code = watch_loop(
            [&] {
                const int c = run_once(out, err);
                std::string names;
                for (const auto& f : watched_files(file)) names += (names.empty() ? "" : ", ") + f.filename().string();
                out << fmt::format(fmt::runtime(tr("pipeline.cli.watching")), fmt::arg("files", names)) << std::endl;
                return c;
            },
            [&] { return watched_files(file); }, [] { return !g_stop.load(); }, [] { std::this_thread::sleep_for(std::chrono::milliseconds{300}); });
        std::signal(SIGINT, previous);
        return g_stop.load() ? 0 : code;
    }

private:
    int run_once(std::ostream& out, std::ostream& err) {
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
        const auto previous = watch_ ? SIG_ERR : std::signal(SIGINT, cancel_on_interrupt);
        const auto result = pipeline::run_graph(*graph, cache, {}, cancel);
        if (!watch_) std::signal(SIGINT, previous);
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
        if (!report_.empty())
            if (auto ok = write_run_report(report_, *doc, *graph, *result, file); !ok) return report_error(err, ok.error()), 1;
        if (json_) print_json(*doc, *graph, *result, out, values_);
        else print_text(*doc, *graph, *result, out, values_);
        return result->checks_passed ? 0 : 3;
    }

    static void print_json(const pipeline::PipelineDoc& doc, const pipeline::Graph& g, const pipeline::RunResult& r, std::ostream& out, std::optional<std::size_t> listed) {
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
                                  {"shape", shape_json(s.shapes)},
                                  {"message", s.message}});
        }
        j["probes"] = nlohmann::json::object();
        for (const auto& [name, value] : r.probes) j["probes"][name] = summary(value, listed);
        j["reports"] = nlohmann::json::array();
        for (const auto& nr : r.reports) {
            nlohmann::json fields = nlohmann::json::object();
            for (const auto& [name, value] : nr.report.fields)
                std::visit([&, &key = name](const auto& x) { fields[key] = x; }, value);
            j["reports"].push_back({{"node", nr.node}, {"passed", nr.report.passed}, {"fields", fields}});
        }
        out << j.dump(2) << '\n';
    }

    static void print_text(const pipeline::PipelineDoc& doc, const pipeline::Graph& g, const pipeline::RunResult& r, std::ostream& out, std::optional<std::size_t> listed) {
        out << fmt::format(fmt::runtime(tr("pipeline.cli.header")), fmt::arg("name", doc.name.empty() ? std::string{"—"} : doc.name),
                           fmt::arg("nodes", g.nodes.size()))
            << '\n';
        std::size_t id_width = 0, type_width = 0, shape_width = 0;
        for (const auto& n : g.nodes) {
            id_width = std::max(id_width, n.id.size());
            type_width = std::max(type_width, n.type->id.size());
        }
        for (const auto& s : r.nodes) shape_width = std::max(shape_width, columns(shape_text(s.shapes)));
        for (const auto i : g.order) {
            const auto& s = r.nodes[i];
            const auto shape = shape_text(s.shapes);
            out << fmt::format("  {} {:<{}}  {:<{}}  {}{}  {:>9.1f} ms  {:>10}", state_mark(s.state), s.id, id_width, g.nodes[i].type->id, type_width, shape,
                               std::string(shape_width - columns(shape), ' '), s.wall_seconds * 1e3, size_text(s.bytes))
                << '\n';
        }
        if (!r.probes.empty()) out << tr("pipeline.cli.probes") << '\n';
        for (const auto& [name, value] : r.probes) {
            const auto j = summary(value, listed);
            std::string line = "  " + name + ": " + j["kind"].get<std::string>();
            if (j.contains("length")) line += fmt::format(" · {} × {}", j["channels"].get<std::size_t>(), j["length"].get<std::size_t>());
            if (j.contains("energy")) line += fmt::format(" · energy {:.6g} · peak {:.6g}", j["energy"].get<double>(), j["peak"].get<double>());
            out << line << '\n';
            if (j.contains("values"))
                for (std::size_t c = 0; c < j["values"].size(); ++c) {
                    std::string row = fmt::format("    ch{}:", c);
                    for (const auto& x : j["values"][c]) row += fmt::format(" {:.6g}", x.get<double>());
                    if (j.value("values_cut", false)) row += " …";
                    out << row << '\n';
                }
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
    std::string file_, output_, probe_dir_, report_;
    std::vector<std::string> sets_;
    std::optional<std::size_t> values_;
    bool json_ = false, watch_ = false;
};

class ExplainCommand final : public Command {
public:
    explicit ExplainCommand(CLI::App& app) : sub_{app.add_subcommand("explain", tr("cli.explain.description"))} {
        sub_->add_option("block", block_, tr("cli.explain.block"))->required();
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto* type = pipeline::find_block_type(block_);
        if (!type) {
            std::vector<std::string> names;
            for (const auto& b : pipeline::block_types()) names.push_back(b.id);
            const auto near = pipeline::nearest_name(block_, names);
            return report_error(err, {core::Errc::invalid_argument, fmt::format(fmt::runtime(tr(near ? "cli.explain.unknown_near" : "cli.explain.unknown")),
                                                                                fmt::arg("block", block_), fmt::arg("near", near.value_or("")))});
        }
        out << type->id << " — " << tr_opt("pipeline.block." + type->id).value_or(type->id) << '\n';
        if (const auto about = tr_opt("pipeline.about." + type->id)) out << *about << '\n';
        const auto ports = [](const std::vector<pipeline::PortSpec>& list) {
            std::string text;
            for (const auto& p : list)
                text += (text.empty() ? "" : ", ") + p.name + " (" + std::string{pipeline::to_string(p.type)} + (p.optional ? ", " + tr("docs.blocks.optional") : "") + ")";
            return text.empty() ? std::string{"—"} : text;
        };
        out << '\n' << tr("cli.explain.inputs") << ' ' << ports(type->inputs) << '\n' << tr("cli.explain.outputs") << ' ' << ports(type->outputs) << '\n';
        if (!type->params.empty()) out << tr("cli.explain.params") << '\n';
        for (const auto& p : type->params) {
            std::string head = "  " + p.name + " = " + (p.fallback.empty() ? std::string{"—"} : p.fallback);
            if (!p.choices.empty()) {
                std::string list;
                for (const auto& c : p.choices) list += (list.empty() ? "" : ", ") + c;
                head += " (" + list + ")";
            } else if (std::isfinite(p.min) && std::isfinite(p.max)) {
                head += fmt::format(" ({:g}…{:g})", p.min, p.max);
            }
            out << head << '\n';
            if (const auto note = tr_opt("pipeline.param." + type->id + "." + p.name)) out << "      " << *note << '\n';
            for (const auto& c : p.choices)
                if (const auto note = tr_opt("pipeline.param." + type->id + "." + p.name + "." + c)) out << "      " << c << " — " << *note << '\n';
        }
        const auto target = theory::block_theory(type->id);
        std::string where;
        if (target.section) {
            if (const auto* s = theory::find_section(target.id)) where = s->number + " " + s->title;
        } else if (const auto* card = theory::find_card(target.id)) {
            where = card->title;
        }
        if (!where.empty()) out << '\n' << fmt::format(fmt::runtime(tr(target.section ? "cli.explain.section" : "cli.explain.card")), fmt::arg("title", where)) << '\n';
        return 0;
    }

private:
    CLI::App* sub_;
    std::string block_;
};

class SweepCommand final : public Command {
public:
    explicit SweepCommand(CLI::App& app) : sub_{app.add_subcommand("sweep", tr("cli.sweep.description"))} {
        sub_->add_option("file", file_, tr("cli.run.file"))->required();
        sub_->add_option("--vary", vary_, tr("cli.sweep.vary"))->required();
        sub_->add_option("--measure", measures_, tr("cli.sweep.measure"))->required();
        sub_->add_option("--set", sets_, tr("cli.run.set"));
        sub_->add_option("--csv", csv_, tr("cli.sweep.csv"));
        sub_->add_option("--svg", svg_, tr("cli.sweep.svg"));
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const std::filesystem::path file{file_};
        auto doc = load(file, err);
        if (!doc) return 1;
        for (const auto& s : sets_)
            if (auto ok = pipeline::apply_set(*doc, s); !ok) return report_error(err, ok.error()), 1;
        auto axis = pipeline::parse_vary(vary_);
        if (!axis) return report_error(err, axis.error()), 1;
        std::vector<pipeline::Measure> measures;
        for (const auto& text : measures_) {
            auto m = pipeline::parse_measure(text);
            if (!m) return report_error(err, m.error()), 1;
            measures.push_back(std::move(*m));
        }
        core::CancelToken cancel;
        g_cancel.store(&cancel);
        const auto previous = std::signal(SIGINT, cancel_on_interrupt);
        const auto result = pipeline::run_sweep(*doc, std::filesystem::absolute(file).parent_path(), std::move(*axis), std::move(measures), cancel);
        std::signal(SIGINT, previous);
        g_cancel.store(nullptr);
        if (!result) return report_error(err, result.error()), result.error().code == core::Errc::cancelled ? 130 : 1;
        const auto param = result->axis.node + "." + result->axis.param;
        if (!csv_.empty())
            if (auto ok = io::write_file_atomic(csv_, sweep_csv(*result)); !ok) return report_error(err, ok.error()), 1;
        if (!svg_.empty())
            if (auto ok = io::write_file_atomic(svg_, svg::to_svg(sweep_chart(*result))); !ok) return report_error(err, ok.error()), 1;
        if (json_) {
            nlohmann::json j{{"param", param}, {"measures", nlohmann::json::array()}, {"rows", nlohmann::json::array()}};
            for (const auto& m : result->measures) j["measures"].push_back(m.text);
            for (const auto& row : result->rows) {
                auto values = nlohmann::json::array();
                for (const auto& v : row.results) values.push_back(v ? nlohmann::json(*v) : nlohmann::json(nullptr));
                j["rows"].push_back({{"value", row.value}, {"results", values}, {"error", row.error}});
            }
            out << j.dump(2) << '\n';
            return 0;
        }
        out << fmt::format(fmt::runtime(tr("pipeline.cli.sweep_header")), fmt::arg("param", param), fmt::arg("count", result->rows.size())) << '\n';
        std::vector<std::string> header{param};
        for (const auto& m : result->measures) header.push_back(m.text);
        std::vector<std::vector<std::string>> cells;
        for (const auto& row : result->rows) {
            std::vector<std::string> line{row.value};
            if (!row.error.empty()) line.push_back(tr("pipeline.cli.sweep_error") + ": " + row.error);
            else
                for (const auto& v : row.results) line.push_back(v ? fmt::format("{:.6g}", *v) : std::string{"—"});
            cells.push_back(std::move(line));
        }
        std::vector<std::size_t> widths(header.size(), 0);
        for (std::size_t c = 0; c < header.size(); ++c) widths[c] = columns(header[c]);
        for (const auto& line : cells)
            for (std::size_t c = 0; c < line.size() && c < widths.size(); ++c)
                if (c + 1 < line.size() || line.size() == widths.size()) widths[c] = std::max(widths[c], columns(line[c]));
        const auto print = [&](const std::vector<std::string>& line) {
            std::string text = " ";
            for (std::size_t c = 0; c < line.size(); ++c)
                text += " " + line[c] + (c + 1 < line.size() && c < widths.size() ? std::string(widths[c] - std::min(widths[c], columns(line[c])) + 1, ' ') : std::string{});
            out << text << '\n';
        };
        print(header);
        for (const auto& line : cells) print(line);
        return 0;
    }

private:
    static std::string sweep_csv(const pipeline::SweepResult& r) {
        std::string out = r.axis.node + "." + r.axis.param;
        for (const auto& m : r.measures) out += "," + m.text;
        out += "\n";
        for (const auto& row : r.rows) {
            out += row.value;
            for (const auto& v : row.results) out += "," + (v ? fmt::format("{}", *v) : std::string{});
            out += "\n";
        }
        return out;
    }

    static svg::Chart sweep_chart(const pipeline::SweepResult& r) {
        svg::Chart c;
        c.title = r.axis.node + "." + r.axis.param;
        std::vector<double> xs;
        bool numeric_axis = true;
        for (std::size_t k = 0; k < r.rows.size(); ++k) {
            const auto v = pipeline::numeric(r.rows[k].value);
            numeric_axis = numeric_axis && v.has_value();
            xs.push_back(v.value_or(static_cast<double>(k)));
        }
        if (!numeric_axis)
            for (std::size_t k = 0; k < xs.size(); ++k) xs[k] = static_cast<double>(k);
        const auto [lo, hi] = xs.empty() ? std::pair{0.0, 1.0} : std::pair{*std::min_element(xs.begin(), xs.end()), *std::max_element(xs.begin(), xs.end())};
        c.x = {lo, hi > lo ? hi : lo + 1, svg::Scale::linear, c.title};
        double ylo = 1e300, yhi = -1e300;
        static constexpr std::array<const char*, 6> kColours{"#0072B2", "#D55E00", "#009E73", "#E69F00", "#56B4E9", "#CC79A7"};
        for (std::size_t j = 0; j < r.measures.size(); ++j) {
            svg::Series s{r.measures[j].text, kColours[j % kColours.size()]};
            for (std::size_t k = 0; k < r.rows.size(); ++k) {
                s.x.push_back(xs[k]);
                const auto v = r.rows[k].results.size() > j ? r.rows[k].results[j] : std::nullopt;
                s.y.push_back(v.value_or(NAN));
                if (v) {
                    ylo = std::min(ylo, *v);
                    yhi = std::max(yhi, *v);
                }
            }
            c.series.push_back(std::move(s));
        }
        if (ylo > yhi) ylo = 0, yhi = 1;
        const double pad = yhi > ylo ? 0.05 * (yhi - ylo) : std::max(1.0, std::abs(yhi)) * 0.05;
        c.y = {ylo - pad, yhi + pad};
        return c;
    }

    CLI::App* sub_;
    std::string file_, vary_, csv_, svg_;
    std::vector<std::string> measures_, sets_;
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
        sub_->add_flag("--json", json_, tr("cli.blocks.json"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream&) override {
        out << (json_ ? pipeline::blocks_json() : markdown_ ? pipeline::blocks_markdown() : pipeline::blocks_text());
        return 0;
    }

private:
    CLI::App* sub_;
    bool markdown_ = false, json_ = false;
};

}  // namespace

void add_pipeline_commands(CLI::App& app, Commands& commands) {
    commands.push_back(std::make_unique<RunCommand>(app));
    commands.push_back(std::make_unique<GraphCommand>(app));
    commands.push_back(std::make_unique<SweepCommand>(app));
    commands.push_back(std::make_unique<BlocksCommand>(app));
    commands.push_back(std::make_unique<ExplainCommand>(app));
}

}  // namespace signals::cli
