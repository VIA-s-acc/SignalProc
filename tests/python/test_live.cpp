#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <format>

#include "commands/cli.hpp"
#include "core/progress.hpp"
#include "io/npy.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/npz_values.hpp"
#include "pipeline/sig.hpp"
#include "python/block_graphs.hpp"
#include "python/form.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;
using Kind = pipeline::ParamSpec::Kind;
namespace fs = std::filesystem;

constexpr const char* kVar = "liveparam";

std::string read(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::string python_exe() {
    const char* p = std::getenv("SIGNALS_PYTHON");
    return p ? p : "python3";
}

bool python_ready() { return std::system((python_exe() + " -c \"import numpy, scipy, pywt\" > " + std::string{SIGNALS_NULL_DEVICE} + " 2>&1").c_str()) == 0; }

std::set<std::string> forms_of(void (*reg)(std::vector<python::PyForm>&)) {
    std::vector<python::PyForm> forms;
    reg(forms);
    std::set<std::string> out;
    for (const auto& f : forms) out.insert(f.block);
    return out;
}

std::vector<const pipeline::BlockType*> group(std::string_view name) {
    const std::map<std::string_view, void (*)(std::vector<python::PyForm>&)> files{{"wavelets", python::register_wavelet_forms}, {"filters", python::register_filter_forms},
                                                                                 {"effects", python::register_effect_forms}, {"analysis", python::register_analysis_forms},
                                                                                 {"rest", python::register_basic_forms}};
    std::vector<const pipeline::BlockType*> out;
    const auto in_file = name == "sources" ? std::set<std::string>{} : forms_of(files.at(name));
    for (const auto& t : pipeline::block_types()) {
        const bool source = t.inputs.empty();
        if (name == "sources" ? source : !source && in_file.contains(t.id)) out.push_back(&t);
    }
    return out;
}

std::string number_text(double v) { return std::format("{}", v); }

std::optional<std::string> bump_number(const std::string& text, double factor, double step) {
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (!(std::isdigit(static_cast<unsigned char>(text[at])) || (text[at] == '.' && at + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[at + 1]))))) continue;
        if (at > 0 && (std::isalpha(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_')) continue;
        auto end = at;
        while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.' || text[end] == 'e')) ++end;
        const auto token = text.substr(at, end - at);
        char* stop = nullptr;
        const double v = std::strtod(token.c_str(), &stop);
        if (stop != token.c_str() + token.size()) continue;
        const bool whole = token.find_first_of(".e") == std::string::npos;
        const double next = whole ? v + step : v * factor;
        return text.substr(0, at) + (whole ? std::format("{}", static_cast<long long>(next)) : number_text(next)) + text.substr(end);
    }
    return std::nullopt;
}

std::vector<std::string> candidates(const pipeline::ParamSpec& s, const std::string& cur) {
    std::vector<std::string> out;
    const auto in_range = [&](double v) { return v >= s.min && v <= s.max; };
    const auto add = [&](std::string v) {
        if (v != cur && std::ranges::find(out, v) == out.end()) out.push_back(std::move(v));
    };
    if (s.name == "wavelet")
        for (const char* w : {"db4", "haar", "db2", "coif1", "bior2.2", "sym2", "sym4"}) add(w);
    if (s.name == "key")
        for (const char* k : {"second", "first", "arr_0"}) add(k);
    if (s.name == "component")
        for (const char* b : {"d1", "a", "d2", "d3", "aa", "ad", "da", "dd"}) add(b);
    if (s.name == "enabled")
        for (const char* b : {"d1", "a", "d1, d2", "all"}) add(b);
    if (s.name == "gains_db")
        for (const char* g : {"d1=-6", "a=-6", "d1=-3, d2=-6"}) add(g);
    if (s.name == "mode")
        for (const char* m : {"zero", "periodization", "symmetric", "reflect"}) add(m);
    if (s.name == "filter")
        for (const char* f : {"db2.h0", "haar.h1", "db4.h0", "db3.f0"}) add(f);
    if (s.name == "range")
        for (const char* r : {"-1, 1", "-0.5, 0.5", "0, 1", "auto"}) add(r);
    if (s.name == "weight")
        for (const char* w : {"1, 2", "2, 1", "1, 10"}) add(w);
    char* stop = nullptr;
    const double v = std::strtod(cur.c_str(), &stop);
    const bool number = !cur.empty() && stop == cur.c_str() + cur.size();
    switch (s.kind) {
    case Kind::integer:
        if (number)
            for (const long long d : {1, -1, 2, -2, 3, 4})
                if (in_range(v + static_cast<double>(d))) add(std::format("{}", static_cast<long long>(v) + d));
        if (number && v >= 2)
            for (const double c : {v * 2, std::floor(v / 2), std::floor(v / 4), std::floor(v / 8)})
                if (in_range(c)) add(std::format("{}", static_cast<long long>(c)));
        break;
    case Kind::real:
        if (number)
            for (const double c : {v * 1.25, v * 0.8, v + 0.5, v - 0.5, v + 1, 0.25, 1.0, 0.5, v * 0.1, v * 10})
                if (in_range(c)) add(number_text(c));
        if (s.name == "rate")
            for (const double c : {8000.0, 16000.0, 44100.0})
                if (in_range(c)) add(number_text(c));
        break;
    case Kind::choice:
        for (const auto& c : s.choices) add(c);
        break;
    case Kind::range:
        for (const char* r : {"1..1", "2..2", "1..2", "2..3", "1..3"}) add(r);
        break;
    case Kind::sigma:
        for (const char* value : {"0.1", "0.5", "1", "mad_d1"}) add(value);
        break;
    case Kind::samples:
        if (cur.ends_with('s')) {
            const double t = std::strtod(cur.c_str(), nullptr);
            add(number_text(t * 1.25) + "s");
            add(number_text(t * 0.8) + "s");
        } else if (number) {
            for (const long long d : {1, -1, 2, 3})
                if (v + static_cast<double>(d) >= 0) add(std::format("{}", static_cast<long long>(v) + d));
            if (v >= 2) {
                add(std::format("{}", static_cast<long long>(v) * 2));
                add(std::format("{}", static_cast<long long>(v) / 2));
            }
        }
        break;
    default:
        for (const auto& [f, d] : std::initializer_list<std::pair<double, double>>{{1.25, 1}, {0.8, -1}, {1.5, 2}})
            if (auto b = bump_number(cur, f, d)) add(*b);
        break;
    }
    return out;
}

std::optional<io::NpzEntries> probes(const pipeline::PipelineDoc& doc, const fs::path& dir) {
    const auto graph = pipeline::build_graph(doc, dir);
    if (!graph) return std::nullopt;
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    const auto result = pipeline::run_graph(*graph, cache, {}, cancel);
    if (!result) return std::nullopt;
    for (const auto& p : graph->probes)
        if (p.name.starts_with("live ") && !result->probes.contains(p.name)) return std::nullopt;
    io::NpzEntries out;
    for (const auto& [name, value] : result->probes) {
        auto entries = pipeline::value_entries(value);
        if (!entries) return std::nullopt;
        for (auto& e : pipeline::prefixed(name, std::move(*entries))) out.push_back(std::move(e));
    }
    return out;
}

bool same(const io::NpzEntries& a, const io::NpzEntries& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (a[k].first != b[k].first || a[k].second.shape != b[k].second.shape || !(a[k].second.data == b[k].second.data)) return false;
    return true;
}

int python_script(const fs::path& sig, const fs::path& out) {
    std::ostringstream o, e;
    const std::string s = sig.string(), d = out.string();
    const std::vector<std::string_view> args{"signals", "python", s, "-o", d};
    return cli::run_cli(args, o, e);
}

std::map<std::string, std::string> exceptions() {
    std::map<std::string, std::string> out;
    std::istringstream in{read(fs::path{SIGNALS_SOURCE_DIR} / "tests" / "python" / "live_exceptions.txt")};
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line.starts_with('#')) continue;
        const auto dash = line.find(" — ");
        out[line.substr(0, dash)] = dash == std::string::npos ? std::string{} : line.substr(dash + 5);
    }
    return out;
}

void check_group(std::string_view name) {
    if (!python_ready()) {
        if (std::getenv("SIGNALS_REQUIRE_BRIDGE")) FAIL("python3 with numpy, scipy and PyWavelets is required: SIGNALS_REQUIRE_BRIDGE is set");
        SKIP("no python3 with numpy, scipy and PyWavelets");
    }
    const auto root = signals::test::temp_path("py-live-check") / std::string{name};
    fs::remove_all(root);
    const auto files = root / "files", jobs = root / "jobs";
    fs::create_directories(jobs);
    signals::test::prepare_block_files(files);
    std::map<std::string, std::string> dead;
    std::size_t made = 0;
    for (const auto* t : group(name)) {
        const auto graphs = signals::test::block_graphs(*t, files);
        if (graphs.empty()) continue;
        for (const auto& spec : t->params) {
            if (spec.kind == Kind::path) continue;
            const auto id = t->id + "." + spec.name;
            std::optional<pipeline::PipelineDoc> base, second;
            bool runs = false;
            for (const auto& g : graphs) {
                const auto loaded = pipeline::load_sig(g.sig);
                REQUIRE(loaded);
                for (std::size_t k = 0; k < loaded->doc.nodes.size() && !(second && base); ++k) {
                    if (loaded->doc.nodes[k].type != t->id) continue;
                    auto doc = loaded->doc;
                    auto& node = doc.nodes[k];
                    const auto cur = node.params.count(spec.name) ? node.params.at(spec.name) : spec.fallback;
                    node.params[spec.name] = std::string{"$"} + kVar;
                    doc.lets.push_back({kVar, cur});
                    for (const auto& out : t->outputs) doc.probes.push_back({{node.id, out.name}, "live " + node.id + " " + out.name});
                    const auto first = probes(doc, files);
                    if (!first) continue;
                    runs = true;
                    for (const auto& c : candidates(spec, cur)) {
                        auto other = doc;
                        other.lets.back().value = c;
                        const auto got = probes(other, files);
                        if (got && !same(*first, *got)) {
                            base = doc;
                            second = other;
                            break;
                        }
                    }
                }
                if (second) break;
            }
            if (!second) {
                dead[id] = runs ? "no second value changes the result in any graph of the block" : "no graph of the block runs with the parameter as a variable";
                continue;
            }
            const auto& doc = *base;
            const auto job = jobs / id;
            fs::create_directories(job);
            const auto first_sig = files / ("live-" + id + "-first.sig"), second_sig = files / ("live-" + id + "-second.sig");
            std::ofstream{first_sig, std::ios::binary} << pipeline::write_sig(doc);
            std::ofstream{second_sig, std::ios::binary} << pipeline::write_sig(*second);
            if (python_script(first_sig, job / "first.py") != 0 || python_script(second_sig, job / "second.py") != 0) {
                dead[id] = "signals python failed";
                fs::remove_all(job);
                continue;
            }
            std::ofstream{job / "var.txt"} << kVar;
            ++made;
        }
    }
    const auto log = root / "live.log";
    const auto cmd = python_exe() + " -I \"" + (fs::path{SIGNALS_SOURCE_DIR} / "tools" / "bridge" / "run_live.py").string() + "\" \"" + jobs.string() + "\" > \"" + log.string() + "\" 2>&1";
    [[maybe_unused]] const int status = std::system(cmd.c_str());
    const auto text = read(log);
    std::istringstream lines{text};
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("DEAD ")) continue;
        const auto id = line.substr(5);
        std::string why;
        for (std::string more; std::getline(lines, more) && more != "END " + id;) why += more + "\n";
        dead[id] = why;
    }
    const auto allowed = exceptions();
    std::string report;
    for (const auto& [id, why] : dead) {
        const auto it = allowed.find(id);
        if (it != allowed.end() && !it->second.empty()) continue;
        report += "\n== " + id + (it != allowed.end() ? " (listed without a reason)" : "") + "\n" + why;
    }
    UNSCOPED_INFO(name << ": " << made << " scripts checked, " << dead.size() << " not live");
    INFO(report);
    CHECK(report.empty());
    CHECK(made > 0);
}

}  // namespace

TEST_CASE("every source param is live in the script", "[py-live-sources]") { check_group("sources"); }
TEST_CASE("every wavelet param is live in the script", "[py-live-wavelets]") { check_group("wavelets"); }
TEST_CASE("every filter param is live in the script", "[py-live-filters]") { check_group("filters"); }
TEST_CASE("every effect param is live in the script", "[py-live-effects]") { check_group("effects"); }
TEST_CASE("every analysis param is live in the script", "[py-live-analysis]") { check_group("analysis"); }
TEST_CASE("every other param is live in the script", "[py-live-rest]") { check_group("rest"); }
