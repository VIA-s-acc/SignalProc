#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/families.hpp"
#include "core/version.hpp"
#include "io/npy.hpp"
#include "i18n/i18n.hpp"
#include "io/generators.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"
#include "python/form.hpp"
#include "python/script.hpp"
#include "support/cli_run.hpp"
#include "support/golden_text.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;

core::Result<python::Script> script_for(const std::string& text, const std::string& name) {
    const auto doc = pipeline::parse_sig(text);
    REQUIRE(doc);
    const auto dir = signals::test::temp_path("py-script");
    std::filesystem::create_directories(dir);
    const auto graph = pipeline::build_graph(*doc, dir);
    REQUIRE(graph);
    core::CancelToken cancel;
    return python::build_script(*doc, *graph, {python::ScriptKind::script, dir / (name + ".py"), name + ".sig", text}, cancel);
}

std::string without_version(std::string text) {
    const std::string v{core::version()};
    for (auto at = text.find(v); at != std::string::npos; at = text.find(v)) text.replace(at, v.size(), "X.Y.Z");
    return text;
}

std::filesystem::path write_sig(const std::string& name, const std::string& text) {
    const auto path = signals::test::temp_path(name);
    std::ofstream{path} << text;
    return path;
}

constexpr const char* kTiny =
    "pipeline \"tiny\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode y = gain(db=-6)\nx -> y\nprobe y as \"y\"\n";

}  // namespace

TEST_CASE("script for a tiny graph matches its golden text", "[py-script]") {
    {
        const LanguageScope ru{Language::ru};
        const auto s = script_for(kTiny, "tiny");
        REQUIRE(s);
        signals::test::check_golden_text("python/tiny_ru", without_version(s->text));
    }
    const LanguageScope en{Language::en};
    const auto s = script_for(kTiny, "tiny");
    REQUIRE(s);
    signals::test::check_golden_text("python/tiny_en", without_version(s->text));
}

TEST_CASE("the data file holds draws, not noise", "[py-script]") {
    const auto s = script_for("pipeline \"n\" v1\nnode x = gen(kind=white, rate=8000, duration=0.01, seed=3, channels=2)\nprobe x as \"x\"\n", "n");
    REQUIRE(s);
    const auto* draws = io::np_find(s->data, "x.draws");
    REQUIRE(draws);
    CHECK(draws->shape == std::vector<std::size_t>{2, 80});
    auto want = io::normal_draws(80, 3);
    const auto second = io::normal_draws(80, 4);
    want.insert(want.end(), second.begin(), second.end());
    CHECK(std::get<std::vector<double>>(draws->data) == want);
    CHECK(s->text.find("noise_from_draws(d, \"white\"") != std::string::npos);
}

TEST_CASE("the check covers probes, reports and side-effect inputs", "[py-script]") {
    const auto s = script_for("pipeline \"w\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nnode w = wav_out(path=\"w.wav\", bits=16)\nx -> w\n", "w");
    INFO((s ? std::string{} : s.error().message));
    REQUIRE(s);
    std::vector<std::string> what;
    for (const auto& c : s->checks) what.push_back(c.what);
    CHECK(std::find(what.begin(), what.end(), "w ← in") != what.end());
    CHECK(std::find(what.begin(), what.end(), "w · clipped") != what.end());
    CHECK(std::find(what.begin(), what.end(), "w · path") != what.end());
}

TEST_CASE("a graph with nothing to check is refused", "[py-script]") {
    const auto s = script_for("pipeline \"e\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\n", "e");
    REQUIRE_FALSE(s);
    CHECK(s.error().message == tr("python.nothing_to_check"));
}

TEST_CASE("signals python reports a graph error like signals run", "[py-script]") {
    const auto sig = write_sig("broken-py.sig", "pipeline \"b\" v1\nnode x = nosuchblock()\nprobe x as \"x\"\n");
    const auto py = signals::test::temp_path("broken-py.py");
    std::filesystem::remove(py);
    auto [rcode, rout, rerr] = signals::test::run_cli({"signals", "run", sig.string()});
    auto [pcode, pout, perr] = signals::test::run_cli({"signals", "python", sig.string()});
    CHECK(pcode == rcode);
    CHECK(perr == rerr);
    CHECK_FALSE(std::filesystem::exists(py));
}

TEST_CASE("signals python names an unsupported node", "[py-script]") {
    const pipeline::BlockType* missing = nullptr;
    for (const auto& t : pipeline::block_types())
        if (!python::find_py_form(t.id) && t.inputs.size() == 1 && t.inputs[0].type == pipeline::PortType::signal && !t.outputs.empty()) missing = &t;
    if (!missing) SKIP("every block has a python form; unsupported() itself is covered in [py-forms]");
    const auto sig = write_sig("unsupported-py.sig", "pipeline \"u\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.25)\nnode odd = " + missing->id +
                                                         "()\nx -> odd\nprobe odd." + missing->outputs[0].name + " as \"b\"\n");
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string()});
    CHECK(code == 1);
    CHECK(err.find("odd") != std::string::npos);
    CHECK(err.find(missing->id) != std::string::npos);
}

TEST_CASE("signals python writes the script and its data", "[py-script]") {
    const auto sig = write_sig("tiny-cli.sig", kTiny);
    const auto py = signals::test::temp_path("tiny-cli.py");
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string()});
    INFO(err);
    REQUIRE(code == 0);
    CHECK(std::filesystem::exists(py));
    CHECK(std::filesystem::exists(signals::test::temp_path("tiny-cli.npz")));
    CHECK(out.find("tiny-cli.py") != std::string::npos);
}

TEST_CASE("only the blocks of the tolerance table weaken tolerance", "[py-script]") {
    const std::map<std::string, double> table{{"iir", 1e-8},        {"remez", 1e-8},     {"firls", 1e-10},         {"lms", 1e-10},        {"sst_band", 1e-10},
                                              {"sst_ridge", 1e-10}, {"cepstrum", 1e-9}, {"time_stretch", 1e-9}, {"pitch_shift", 1e-9}};
    for (const auto& f : python::py_forms()) {
        INFO(f.block);
        if (f.tol == 1e-12) continue;
        REQUIRE(table.contains(f.block));
        CHECK(table.at(f.block) == f.tol);
    }
    for (const auto& [block, tol] : table) {
        INFO(block);
        const auto* f = python::find_py_form(block);
        REQUIRE(f);
        CHECK(f->tol == tol);
    }
}

TEST_CASE("the first two lines of a script are fixed", "[py-script]") {
    const auto s = script_for("pipeline \"coding: raw_unicode_escape\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.01)\nprobe x as \"x\"\n", "coding_raw_unicode_escape");
    REQUIRE(s);
    const auto first = s->text.substr(0, s->text.find('\n'));
    const auto rest = s->text.substr(first.size() + 1);
    const auto second = rest.substr(0, rest.find('\n'));
    CHECK(first == "# -*- coding: utf-8 -*-");
    CHECK(second.find("coding") == std::string::npos);
    CHECK(second.find("raw_unicode_escape") == std::string::npos);
}

TEST_CASE("tolerance follows the weakest block upstream", "[py-script]") {
    const auto s = script_for("pipeline \"t\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.05)\nnode f = iir(cutoff=1000)\nnode g = gain(db=-3)\nx -> f -> g\nprobe g as \"g\"\nprobe x as \"x\"\n",
                              "t");
    REQUIRE(s);
    for (const auto& c : s->checks) {
        INFO(c.what);
        if (c.what == "g") {
            CHECK(c.tol == 1e-8);
            CHECK(c.origin == "f");
        }
        if (c.what == "x") {
            CHECK(c.tol == 1e-12);
            CHECK(c.origin.empty());
        }
    }
}

TEST_CASE("a symlet goes to pywt with the filters of Signals", "[py-script]") {
    const auto s = script_for("pipeline \"s\" v1\nnode x = gen(kind=sine, rate=8000, duration=0.05)\nnode d = dwt(wavelet=sym8, levels=2)\nx -> d\nprobe d as \"d\"\n", "s");
    REQUIRE(s);
    CHECK(s->text.find("pywt.Wavelet(\"sym8\", filter_bank=") != std::string::npos);
    CHECK(io::np_find(s->data, "wavelet.sym8.dec_lo"));
    CHECK(s->text.find("# Детали cD") != std::string::npos);
}

TEST_CASE("a script named like a Python module is refused", "[py-script]") {
    for (const char* name : {"math.py", "numpy.py", "signal.py", "pywt.py", "random.py"}) {
        INFO(name);
        CHECK_FALSE(python::check_script_name(name));
    }
    for (const char* name : {"first_steps.py", "Math.py", "math-2.py", "lesson.ipynb", "math.ipynb"}) {
        INFO(name);
        CHECK(python::check_script_name(name));
    }
    const auto sig = write_sig("math.sig", kTiny);
    std::filesystem::remove(signals::test::temp_path("math.py"));
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string()});
    CHECK(code == 2);
    CHECK(err.find("math.py") != std::string::npos);
    CHECK(err.find("-o") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(signals::test::temp_path("math.py")));
}

TEST_CASE("signals python writes only a .py script or an .ipynb notebook", "[py-script]") {
    for (const char* name : {"g.npz", "g.sig", "g.txt", "g"}) {
        INFO(name);
        CHECK_FALSE(python::check_script_name(name));
    }
    const auto sig = write_sig("ext-cli.sig", kTiny);
    const auto npz = signals::test::temp_path("ext-cli.npz");
    std::filesystem::remove(npz);
    auto [code, out, err] = signals::test::run_cli({"signals", "python", sig.string(), "-o", npz.string()});
    CHECK(code == 2);
    CHECK(err.find(".ipynb") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(npz));
    std::ifstream in{sig};
    const std::string kept{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(kept == kTiny);
    auto [same, sout, serr] = signals::test::run_cli({"signals", "python", sig.string(), "-o", sig.string()});
    CHECK(same == 2);
}

TEST_CASE("a bypassed node passes its input through in the script", "[py-script]") {
    const auto s = script_for("pipeline \"b\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode g = gain(db=-6) bypass\nnode r = stats()\nnode q = assert() bypass\n"
                              "x -> g\ng -> r\nr -> q\nprobe g as \"y\"\n",
                              "bypass");
    REQUIRE(s);
    CHECK(s->text.find("\ng = x") != std::string::npos);
    CHECK(s->text.find("10 ** (") == std::string::npos);
    CHECK(s->text.find("\nq = r\nq_ok = r_ok") != std::string::npos);
}

namespace {

std::set<std::string> import_bindings(const std::string& line) {
    std::set<std::string> out;
    std::istringstream words{line.substr(line.starts_with("from ") ? line.find(" import ") + 8 : 7)};
    for (std::string part; std::getline(words, part, ',');) {
        while (part.starts_with(' ')) part.erase(0, 1);
        const auto as = part.find(" as ");
        auto name = as == std::string::npos ? part : part.substr(as + 4);
        if (!line.starts_with("from ")) name = name.substr(0, name.find('.'));
        while (name.ends_with(' ')) name.pop_back();
        out.insert(name);
    }
    return out;
}

}  // namespace

TEST_CASE("python import bindings never shadow a function", "[py-script]") {
    std::set<std::string> defined;
    for (const auto& f : python::python_functions()) {
        std::istringstream lines{std::string{f.text}};
        for (std::string line; std::getline(lines, line);)
            if (line.starts_with("def ")) defined.insert(line.substr(4, line.find('(') - 4));
    }
    for (const auto& f : python::python_functions())
        for (const auto& line : python::function_imports(f.text))
            for (const auto& name : import_bindings(line)) {
                INFO(f.name << ": " << line);
                CHECK_FALSE(defined.contains(name));
            }
}

TEST_CASE("node names avoid imports, helpers and form temporaries", "[py-script]") {
    const auto s = script_for("pipeline \"n\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode s = stats()\nnode s_ok = gain(db=-1)\n"
                              "node io = gain(db=-2)\nnode display = gain(db=-3)\nnode _stats_spectrum = gain(db=-4)\nnode x_v = gain(db=-5)\n"
                              "x -> s\nx -> s_ok\nx -> io\nx -> display\nx -> _stats_spectrum\nx -> x_v\n"
                              "probe s_ok as \"a\"\nprobe io as \"b\"\nprobe display as \"c\"\nprobe _stats_spectrum as \"d\"\nprobe x_v as \"e\"\n",
                              "names");
    REQUIRE(s);
    for (const char* var : {"\ns_ok_ = ", "\nio_ = ", "\ndisplay_ = ", "\n_stats_spectrum_ = ", "\nx_v_ = "}) {
        INFO(var);
        CHECK(s->text.find(var) != std::string::npos);
    }
}

TEST_CASE("every temporary suffix a form writes is reserved", "[py-script]") {
    const std::regex pattern{R"(\{[0-9a-z]*\}_([A-Za-z0-9]+)|" ?\+ ?"_([A-Za-z0-9]+)|out\[[0-9]\] \+ "_([A-Za-z0-9]+))"};
    const auto suffixes = python::temporary_suffixes();
    for (const auto& file : std::filesystem::directory_iterator{std::filesystem::path{SIGNALS_SOURCE_DIR} / "src" / "python"}) {
        if (file.path().extension() != ".cpp") continue;
        std::ifstream in{file.path()};
        const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        for (auto it = std::sregex_iterator{text.begin(), text.end(), pattern}; it != std::sregex_iterator{}; ++it) {
            const auto suffix = (*it)[1].matched ? (*it)[1].str() : (*it)[2].matched ? (*it)[2].str() : (*it)[3].str();
            INFO(file.path().filename().string() << ": _" << suffix);
            CHECK(std::ranges::find(suffixes, suffix) != suffixes.end());
        }
    }
}

TEST_CASE("a wavelet from the user library goes to pywt with its filters", "[py-script]") {
    const auto db2 = core::wavelet("db2");
    REQUIRE(db2);
    core::set_user_wavelets([db2](std::string_view name) -> std::optional<core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db2;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    const auto s = script_for("pipeline \"u\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode c = dwt(wavelet=my_bank, levels=2)\nx -> c\nprobe c as \"c\"\n", "user-bank");
    core::set_user_wavelets({}, {});
    REQUIRE(s);
    CHECK(s->text.find("pywt.Wavelet(\"my_bank\", filter_bank=") != std::string::npos);
    CHECK(io::np_find(s->data, "wavelet.my_bank.dec_lo"));
}

TEST_CASE("xcorr in a script switches to the FFT like the program", "[py-script]") {
    const auto s = script_for("pipeline \"c\" v1\nnode x = gen(kind=sine, freq=440, rate=8000, duration=0.01)\nnode y = gen(kind=sine, freq=330, rate=8000, duration=0.01)\n"
                              "node c = xcorr()\nx -> c.a\ny -> c.b\nprobe c as \"c\"\n",
                              "xcorr-fft");
    REQUIRE(s);
    CHECK(s->text.find("def correlate_full(") != std::string::npos);
    CHECK(s->text.find("np.correlate(") == std::string::npos);
}
