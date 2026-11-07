#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/metrics.hpp"
#include "pipeline/block.hpp"
#include "support/golden.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

core::Result<std::vector<Value>> attempt(std::string_view type, std::vector<Value> inputs, const Params& params = {}) {
    const auto* t = find_block_type(type);
    REQUIRE(t);
    auto resolved = resolve_params(*t, params);
    INFO(type << ": " << (resolved ? std::string{} : resolved.error().message));
    REQUIRE(resolved);
    RunContext ctx{std::filesystem::temp_directory_path()};
    return t->run(ctx, inputs, *resolved);
}

std::vector<Value> run(std::string_view type, std::vector<Value> inputs, const Params& params = {}) {
    auto out = attempt(type, std::move(inputs), params);
    INFO(type << ": " << (out ? std::string{} : out.error().message));
    REQUIRE(out);
    return *out;
}

std::string error_of(std::string_view type, std::vector<Value> inputs, const Params& params = {}) {
    auto out = attempt(type, std::move(inputs), params);
    REQUIRE_FALSE(out);
    return out.error().message;
}

const core::AudioBuffer& audio(const Value& v) { return std::get<core::AudioBuffer>(v); }
const std::vector<double>& mono(const Value& v) { return audio(v).channels.at(0); }
const Report& report(const Value& v) { return std::get<Report>(v); }

core::AudioBuffer buffer(double rate, std::vector<std::vector<double>> channels) { return {rate, std::move(channels)}; }

const nlohmann::json& golden() {
    static const auto g = signals::test::load_golden("blocks_analysis.json");
    return g;
}

std::vector<double> vec(const nlohmann::json& j) { return j.get<std::vector<double>>(); }
double rate() { return golden()["fs"].get<double>(); }
core::AudioBuffer input(const char* key = "x") { return buffer(rate(), {vec(golden()[key])}); }

std::string list(const nlohmann::json& j) {
    std::ostringstream out;
    out.precision(17);
    for (const auto& v : j) out << (out.tellp() > 0 ? " " : "") << v.get<double>();
    return out.str();
}

double field(const Report& r, const std::string& name) {
    for (const auto& [k, v] : r.fields)
        if (k == name) return std::get<double>(v);
    FAIL("no field " << name);
    return 0;
}

std::string text_field(const Report& r, const std::string& name) {
    for (const auto& [k, v] : r.fields)
        if (k == name) return std::get<std::string>(v);
    FAIL("no field " << name);
    return {};
}

double close(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    return core::max_abs_error(a, b);
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

}  // namespace

TEST_CASE("lfilter matches scipy lfilter and refuses a zero or unstable denominator", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["lfilter"]) {
        INFO(list(c["b"]) << " / " << list(c["a"]));
        CHECK(close(mono(run("lfilter", {input()}, {{"b", list(c["b"])}, {"a", list(c["a"])}})[0]), vec(c["y"])) < 1e-10);
    }
    CHECK(error_of("lfilter", {input()}, {{"a", "0, 1"}}).find("a[0]") != std::string::npos);
    CHECK(error_of("lfilter", {input()}, {{"a", "1 -1.5"}}).find("1.5") != std::string::npos);
    CHECK(mono(run("lfilter", {input()}, {{"b", "1"}, {"a", "1 -2 1"}})[0]).size() == 300);
    CHECK_FALSE(attempt("lfilter", {input()}, {{"b", "1, x"}}));
}

TEST_CASE("fir_taps convolves like numpy and compensates the delay", "[pipeline][blocks][analysis]") {
    const auto& g = golden()["fir_taps"];
    CHECK(close(mono(run("fir_taps", {input()}, {{"taps", list(g["taps"])}})[0]), vec(g["y"])) < 1e-12);
    CHECK(close(mono(run("fir_taps", {input()}, {{"taps", list(g["taps"])}, {"compensate", "yes"}})[0]), vec(g["centred"])) < 1e-12);
    CHECK(mono(run("fir_taps", {buffer(8, {{1, 2, 3}})})[0]) == std::vector<double>{1, 2, 3});
}

TEST_CASE("savgol matches scipy savgol_filter in every edge mode", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["savgol"]) {
        const Params p{{"window", std::to_string(c["window"].get<int>())}, {"polyorder", std::to_string(c["polyorder"].get<int>())},
                       {"deriv", std::to_string(c["deriv"].get<int>())}, {"mode", c["mode"].get<std::string>()}};
        INFO(c["window"].get<int>() << " " << c["polyorder"].get<int>() << " " << c["deriv"].get<int>() << " " << c["mode"].get<std::string>());
        const auto y = mono(run("savgol", {input()}, p)[0]);
        const auto expected = vec(c["y"]);
        double scale = 1;
        for (const double v : expected) scale = std::max(scale, std::abs(v));
        CHECK(close(y, expected) < 1e-10 * scale);
    }
    for (const auto& c : golden()["savgol_short"]) {
        INFO(c["mode"].get<std::string>());
        CHECK(close(mono(run("savgol", {buffer(rate(), {vec(c["x"])})}, {{"window", "11"}, {"polyorder", "2"}, {"mode", c["mode"].get<std::string>()}})[0]),
                    vec(c["y"])) < 1e-12);
    }
    CHECK(error_of("savgol", {input()}, {{"window", "10"}}).find("10") != std::string::npos);
    CHECK(error_of("savgol", {input()}, {{"window", "5"}, {"polyorder", "5"}}).find("5") != std::string::npos);
    CHECK(error_of("savgol", {input()}, {{"polyorder", "2"}, {"deriv", "3"}}).find("3") != std::string::npos);
    CHECK(error_of("savgol", {buffer(rate(), {{1, 2, 3}})}).find("11") != std::string::npos);
    CHECK_FALSE(attempt("savgol", {buffer(0, {{1, 2, 3, 4, 5}})}, {{"window", "5"}, {"deriv", "1"}}));
    std::vector<double> ramp(50);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = 0.5 * static_cast<double>(i) / 1000;
    const auto slope = run("savgol", {buffer(1000, {ramp})}, {{"deriv", "1"}});
    for (const double v : mono(slope[0])) CHECK(std::abs(v - 0.5) < 1e-10);
}

TEST_CASE("hilbert gives the imaginary part and the phase of scipy hilbert", "[pipeline][blocks][analysis]") {
    for (const char* output : {"imag", "phase", "unwrapped"}) {
        INFO(output);
        CHECK(close(mono(run("hilbert", {input()}, {{"output", output}})[0]), vec(golden()["hilbert"][output])) < 1e-10);
    }
    CHECK(mono(run("hilbert", {buffer(8, {{-2}})}, {{"output", "imag"}})[0]) == std::vector<double>{0});
}

TEST_CASE("remez and firls design in Hz and filter like scipy", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["remez"]) {
        Params p{{"numtaps", std::to_string(c["numtaps"].get<int>())}, {"bands", list(c["bands"])}, {"desired", list(c["desired"])},
                 {"type", c["type"].get<std::string>()}};
        if (!c["weight"].is_null()) p["weight"] = list(c["weight"]);
        INFO(c["numtaps"].get<int>() << " " << c["type"].get<std::string>());
        CHECK(close(mono(run("remez", {input()}, p)[0]), vec(c["y"])) < 1e-8);
        p["compensate"] = "yes";
        CHECK(close(mono(run("remez", {input()}, p)[0]), vec(c["centred"])) < 1e-8);
    }
    for (const auto& c : golden()["firls"]) {
        Params p{{"numtaps", std::to_string(c["numtaps"].get<int>())}, {"bands", list(c["bands"])}, {"desired", list(c["desired"])}};
        if (!c["weight"].is_null()) p["weight"] = list(c["weight"]);
        INFO(c["numtaps"].get<int>());
        CHECK(close(mono(run("firls", {input()}, p)[0]), vec(c["y"])) < 1e-10);
        p["compensate"] = "yes";
        CHECK(close(mono(run("firls", {input()}, p)[0]), vec(c["centred"])) < 1e-10);
    }
    const auto nyquist = mono(run("remez", {input()}, {{"numtaps", "63"}, {"bands", "0 800 1200 nyquist"}})[0]);
    CHECK(close(nyquist, vec(golden()["remez"][0]["y"])) < 1e-8);
    CHECK(error_of("remez", {input()}, {{"bands", "0 800 1200 5000"}}).find("5000") != std::string::npos);
    CHECK(error_of("firls", {buffer(0, {{1, 2}})}).find("firls") != std::string::npos);
    CHECK_FALSE(attempt("firls", {input()}, {{"numtaps", "30"}}));
    CHECK_FALSE(attempt("remez", {input()}, {{"weight", "1 2 3"}}));
}

TEST_CASE("pitch follows a straightforward YIN and reports voiced frames", "[pipeline][blocks][analysis]") {
    const auto& g = golden()["pitch"];
    const Params p{{"fmin", "80"}, {"fmax", "600"}, {"frame", "512"}, {"hop", "128"}, {"threshold", "0.15"}};
    const auto out = run("pitch", {input("tone")}, p);
    const auto& track = audio(out[1]);
    CHECK(track.sample_rate == rate() / 128);
    const auto expected = vec(g["f0"]);
    REQUIRE(track.channels.at(0).size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(near(track.channels[0][i], expected[i], 1e-10));
    CHECK(near(field(report(out[0]), "f0_median_hz"), g["f0_median_hz"].get<double>(), 1e-10));
    CHECK(field(report(out[0]), "voiced_fraction") == g["voiced_fraction"].get<double>());
    CHECK(field(report(out[0]), "frames") == g["frames"].get<double>());
    CHECK(error_of("pitch", {input("tone")}, {{"fmin", "10"}, {"frame", "512"}}).find("1600") != std::string::npos);
    CHECK_FALSE(attempt("pitch", {input("tone")}, {{"fmin", "500"}, {"fmax", "400"}}));
    const auto silent = run("pitch", {buffer(8000, {std::vector<double>(4096, 0.0)})});
    CHECK(field(report(silent[0]), "voiced_fraction") == 0);
    CHECK(field(report(silent[0]), "f0_median_hz") == 0);
}

TEST_CASE("mfcc averages the DCT of the log mel energies", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["mfcc"]) {
        const Params p{{"n_mfcc", std::to_string(c["n_mfcc"].get<int>())}, {"n_mels", std::to_string(c["n_mels"].get<int>())},
                       {"frame", std::to_string(c["frame"].get<int>())}, {"hop", std::to_string(c["hop"].get<int>())}};
        const auto r = report(run("mfcc", {input("tone")}, p)[0]);
        CHECK(field(r, "frames") == c["frames"].get<double>());
        const auto expected = vec(c["mean"]);
        for (std::size_t k = 0; k < expected.size(); ++k) {
            INFO(k);
            CHECK(near(field(r, "mfcc" + std::to_string(k)), expected[k], 1e-10));
        }
    }
    CHECK_FALSE(attempt("mfcc", {input("tone")}, {{"n_mfcc", "30"}, {"n_mels", "20"}}));
}

TEST_CASE("spectral_features match the numpy reference", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["spectral_features"]) {
        const Params p{{"frame", std::to_string(c["frame"].get<int>())}, {"hop", std::to_string(c["hop"].get<int>())},
                       {"rolloff_pct", std::to_string(c["rolloff_pct"].get<double>())}};
        const auto r = report(run("spectral_features", {input("tone")}, p)[0]);
        for (const auto& [name, value] : c["report"].items()) {
            INFO(c["frame"].get<int>() << " " << name);
            CHECK(near(field(r, name), value.get<double>(), 1e-10));
        }
    }
    const auto quiet = report(run("spectral_features", {buffer(8000, {std::vector<double>(4096, 0.0)})})[0]);
    CHECK(field(quiet, "centroid_hz") == 0);
    CHECK(field(quiet, "flatness") == 0);
}

TEST_CASE("histogram counts like numpy histogram", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["histogram"]) {
        const auto r = report(run("histogram", {buffer(rate(), {vec(c["x"])})}, {{"bins", std::to_string(c["bins"].get<int>())}, {"range", c["range"].get<std::string>()}})[0]);
        std::string counts;
        for (const auto& v : c["counts"]) counts += (counts.empty() ? "" : " ") + std::to_string(v.get<int>());
        INFO(c["range"].get<std::string>());
        CHECK(text_field(r, "counts") == counts);
        CHECK(field(r, "lo") == c["lo"].get<double>());
        CHECK(field(r, "hi") == c["hi"].get<double>());
        CHECK(field(r, "outside") == c["outside"].get<double>());
        CHECK(near(field(r, "mode"), c["mode"].get<double>(), 1e-15));
    }
    CHECK(text_field(report(run("histogram", {buffer(8, {{0, 0.5, 1}})}, {{"bins", "2"}})[0]), "counts") == "1 2");
    CHECK(error_of("histogram", {input()}, {{"range", "1 0"}}).find("1 0") != std::string::npos);
    CHECK(error_of("histogram", {buffer(8, {{1, NAN}})}).find("histogram") != std::string::npos);
    const auto two = report(run("histogram", {buffer(8, {{1, 2}, {3}})}, {{"bins", "1"}})[0]);
    CHECK(text_field(two, "counts_ch1") == "1");
}

TEST_CASE("delay_estimate finds the lag of b behind a", "[pipeline][blocks][analysis]") {
    const auto a = buffer(rate(), {vec(golden()["delay_a"])});
    for (const auto& c : golden()["delay"]) {
        const auto r = report(run("delay_estimate", {a, buffer(rate(), {vec(c["b"])})})[0]);
        for (const auto& [name, value] : c["report"].items()) {
            INFO(name);
            CHECK(near(field(r, name), value.get<double>(), 1e-10));
        }
    }
    CHECK(error_of("delay_estimate", {a, buffer(16000, {{1}})}).find("16000") != std::string::npos);
    CHECK(error_of("delay_estimate", {a, buffer(rate(), {{}})}).find("delay_estimate") != std::string::npos);
    CHECK(error_of("delay_estimate", {buffer(0, {{1}}), buffer(0, {{1}})}).find("delay_estimate") != std::string::npos);
    const auto quiet = report(run("delay_estimate", {buffer(8, {{0, 0, 0}}), buffer(8, {{1, 2}})})[0]);
    CHECK(field(quiet, "lag_samples") == 0);
    CHECK(field(quiet, "peak_corr") == 0);
}

TEST_CASE("correlation reports Pearson, Spearman and cosine over the common prefix", "[pipeline][blocks][analysis]") {
    for (const auto& c : golden()["correlation"]) {
        const auto r = report(run("correlation", {buffer(rate(), {vec(c["a"])}), buffer(rate(), {vec(c["b"])})})[0]);
        for (const char* name : {"pearson", "spearman", "cosine", "n"}) {
            INFO(name);
            CHECK(near(field(r, name), c[name].get<double>(), 1e-12));
        }
    }
    const auto flat = report(run("correlation", {buffer(8, {{1, 1, 1}}), buffer(8, {{1, 2, 3}})})[0]);
    CHECK(field(flat, "pearson") == 0);
    CHECK(field(flat, "spearman") == 0);
    CHECK(near(field(flat, "cosine"), 6 / std::sqrt(42.0), 1e-15));
}

TEST_CASE("coeff_stats reports energy share, entropy and sparsity per band", "[pipeline][blocks][analysis]") {
    const auto& g = golden()["coeff_stats"];
    const auto dwt = run("dwt", {input()}, {{"wavelet", "db2"}, {"levels", "3"}})[0];
    const auto r = report(run("coeff_stats", {dwt}, {{"tol", "0.05"}})[0]);
    for (const auto& [band, values] : g["bands"].items())
        for (const auto& [name, value] : values.items()) {
            INFO(band << "." << name);
            CHECK(near(field(r, "c0." + band + "." + name), value.get<double>(), 1e-12));
        }
    const auto all_x = vec(golden()["x"]);
    const auto swt = run("swt", {buffer(rate(), {std::vector<double>(all_x.begin(), all_x.begin() + 256)})}, {{"wavelet", "db2"}, {"levels", "2"}})[0];
    const auto energy = report(run("band_energy", {swt})[0]);
    const auto stats = report(run("coeff_stats", {swt})[0]);
    for (const char* band : {"a2", "d2", "d1"}) CHECK(near(field(stats, std::string{"c0."} + band + ".energy"), field(energy, std::string{"c0."} + band), 1e-15));
    const auto zero = report(run("coeff_stats", {run("dwt", {buffer(8, {std::vector<double>(16, 0.0)})}, {{"levels", "1"}})[0]})[0]);
    CHECK(field(zero, "c0.d1.sparsity") == 1);
    CHECK(field(zero, "c0.d1.entropy") == 0);
    CHECK(field(zero, "c0.d1.energy") == 0);
}

TEST_CASE("the analysis blocks survive empty, silent, one-sample and rateless inputs", "[pipeline][blocks][analysis]") {
    const std::vector<std::string> ids{"lfilter", "fir_taps", "savgol", "hilbert", "remez", "firls", "pitch", "mfcc", "spectral_features",
                                       "histogram", "delay_estimate", "correlation", "coeff_stats"};
    const std::vector<core::AudioBuffer> signals{buffer(8000, {}), buffer(8000, {{}}), buffer(8000, {{0.0}}), buffer(8000, {{0.5}, {-0.5}}), buffer(0, {{1, 2}})};
    for (const auto& id : ids) {
        const auto* t = find_block_type(id);
        REQUIRE(t);
        const auto params = resolve_params(*t, {});
        REQUIRE(params);
        for (const auto& s : signals) {
            std::vector<Value> inputs;
            for (const auto& port : t->inputs) inputs.push_back(port.type == PortType::coeffs ? Value{Coeffs{}} : Value{s});
            RunContext ctx{std::filesystem::temp_directory_path()};
            const auto out = t->run(ctx, inputs, *params);
            INFO(id << " on " << s.channels.size() << " channels at " << s.sample_rate << " Hz");
            if (!out) {
                CHECK_FALSE(out.error().message.empty());
                continue;
            }
            CHECK(out->size() == t->outputs.size());
            for (const auto& v : *out) {
                if (const auto* a = std::get_if<core::AudioBuffer>(&v))
                    for (const auto& c : a->channels)
                        for (const double x : c) CHECK(std::isfinite(x));
                if (const auto* r = std::get_if<Report>(&v))
                    for (const auto& [name, value] : r->fields)
                        if (const auto* d = std::get_if<double>(&value)) CHECK(std::isfinite(*d));
            }
        }
    }
}

TEST_CASE("histogram stays inside its bins for huge and degenerate ranges", "[pipeline][blocks][analysis][review]") {
    const auto x = buffer(8, {std::vector<double>{-1e307, -1.0, 0.0, 1.0, 1e307, 5e306}});
    const auto wide = report(run("histogram", {x}, {{"bins", "4"}, {"range", "-1.7e308 1.7e308"}})[0]);
    CHECK(field(wide, "in_range") == 6);
    CHECK(text_field(wide, "counts") == "0 2 4 0");
    const auto all = report(run("histogram", {x}, {{"bins", "3"}})[0]);
    CHECK(field(all, "in_range") == 6);
    const auto flat = report(run("histogram", {buffer(8, {std::vector<double>(5, 2.0)})}, {{"bins", "3"}})[0]);
    CHECK(field(flat, "in_range") == 5);
}

TEST_CASE("pitch and reverb refuse an absurd sample rate with a clear message", "[pipeline][blocks][analysis][review]") {
    const auto x = buffer(9e26, {std::vector<double>(64, 0.5)});
    for (const char* id : {"pitch", "reverb"}) {
        INFO(id);
        const auto* t = find_block_type(id);
        REQUIRE(t);
        const auto params = resolve_params(*t, {});
        REQUIRE(params);
        const auto out = t->run(RunContext{std::filesystem::temp_directory_path()}, std::vector<Value>{x}, *params);
        REQUIRE_FALSE(out);
        CHECK(out.error().message.find("18446744073709551") == std::string::npos);
        CHECK(out.error().message.find(id) != std::string::npos);
    }
}

TEST_CASE("coeff_stats names wavelet-packet bands by their paths", "[pipeline][blocks][analysis][review]") {
    const auto w = run("wpt", {input()}, {{"wavelet", "db2"}, {"levels", "2"}, {"basis", "full"}})[0];
    const auto r = report(run("coeff_stats", {w})[0]);
    const auto& c = std::get<signals::pipeline::Coeffs>(w);
    for (std::size_t b = 0; b < c.channels.front().bands.size(); ++b) {
        const auto name = signals::pipeline::band_label(c, b);
        INFO(name);
        bool found = false;
        for (const auto& [k, v] : r.fields) found = found || k.find("." + name + ".energy") != std::string::npos || k == name + ".energy";
        CHECK(found);
    }
}
