#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <locale>
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
const Coeffs& coeffs(const Value& v) { return std::get<Coeffs>(v); }
const Report& report(const Value& v) { return std::get<Report>(v); }

core::AudioBuffer buffer(double rate, std::vector<std::vector<double>> channels) { return {rate, std::move(channels)}; }

const nlohmann::json& golden() {
    static const auto g = signals::test::load_golden("blocks2.json");
    return g;
}

std::vector<double> vec(const nlohmann::json& j) { return j.get<std::vector<double>>(); }
core::AudioBuffer golden_input() { return buffer(golden()["fs"].get<double>(), {vec(golden()["x"])}); }

std::string text(double v) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << v;
    return out.str();
}

double field(const Report& r, const std::string& name) {
    for (const auto& [k, v] : r.fields)
        if (k == name) return std::get<double>(v);
    FAIL("no field " << name);
    return 0;
}

double close(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    return core::max_abs_error(a, b);
}

double close_bands(const Coeffs& c, std::size_t channel, const nlohmann::json& bands) {
    REQUIRE(c.channels.at(channel).bands.size() == bands.size());
    double worst = 0;
    for (std::size_t b = 0; b < bands.size(); ++b) worst = std::max(worst, close(c.channels[channel].bands[b], vec(bands[b])));
    return worst;
}

Value dwt_of_input() { return run("dwt", {golden_input()}, {{"wavelet", "db2"}, {"levels", "3"}})[0]; }

}  // namespace

TEST_CASE("polyphase splits into x[2n] and x[2n+1] and ipolyphase interleaves them back", "[pipeline][blocks2]") {
    const auto stereo = buffer(1000, {{1, 2, 3, 4, 5}, {-1, -2}});
    const auto parts = run("polyphase", {stereo});
    REQUIRE(parts.size() == 2);
    CHECK(audio(parts[0]).channels == std::vector<std::vector<double>>{{1, 3, 5}, {-1}});
    CHECK(audio(parts[1]).channels == std::vector<std::vector<double>>{{2, 4}, {-2}});
    CHECK(audio(parts[0]).sample_rate == 500);
    const auto back = audio(run("ipolyphase", {parts[0], parts[1]})[0]);
    CHECK(back.channels == stereo.channels);
    CHECK(back.sample_rate == 1000);
    const auto in = golden_input();
    const auto split = run("polyphase", {in});
    CHECK(mono(run("ipolyphase", {split[0], split[1]})[0]) == in.channels[0]);
    CHECK(error_of("ipolyphase", {buffer(500, {{1, 2, 3}}), buffer(500, {{1}})}).find("ipolyphase") != std::string::npos);
    CHECK(error_of("ipolyphase", {buffer(500, {{1}, {2}}), buffer(500, {{1}})}).find("2") != std::string::npos);
    CHECK(error_of("ipolyphase", {buffer(500, {{1}}), buffer(400, {{1}})}).find("400") != std::string::npos);
}

TEST_CASE("keep_largest keeps the K largest coefficients like a numpy stable argsort", "[pipeline][blocks2]") {
    const auto dwt = dwt_of_input();
    for (const auto& c : golden()["keep_largest"]) {
        const Params p{{"mode", c["mode"].get<std::string>()}, {"amount", text(c["amount"].get<double>())}, {"keep_approx", c["keep_approx"].get<bool>() ? "yes" : "no"}};
        INFO(c["mode"].get<std::string>() << " " << c["amount"].get<double>());
        CHECK(close_bands(coeffs(run("keep_largest", {dwt}, p)[0]), 0, c["bands"]) < 1e-10);
    }
    const auto all = coeffs(run("keep_largest", {dwt}, {{"mode", "count"}, {"amount", "1e9"}, {"keep_approx", "no"}})[0]);
    CHECK(all.channels[0].bands == coeffs(dwt).channels[0].bands);
    const auto none = coeffs(run("keep_largest", {dwt}, {{"amount", "0"}})[0]);
    CHECK(none.channels[0].bands[0] == coeffs(dwt).channels[0].bands[0]);
    CHECK(std::ranges::all_of(none.channels[0].bands.back(), [](double v) { return v == 0; }));
    CHECK(error_of("keep_largest", {dwt}, {{"amount", "150"}}).find("150") != std::string::npos);
    auto bad = coeffs(dwt);
    bad.channels[0].bands[1][0] = std::nan("");
    CHECK(error_of("keep_largest", {Value{bad}}).find("keep_largest") != std::string::npos);
}

TEST_CASE("coeff_quantize rounds the details to a mid-tread grid", "[pipeline][blocks2]") {
    const auto dwt = dwt_of_input();
    for (const auto& c : golden()["coeff_quantize"]) {
        const Params p{{"mode", c["mode"].get<std::string>()}, {"bits", std::to_string(std::max(2, c["bits"].get<int>()))}, {"step", text(std::max(1e-12, c["step"].get<double>()))}};
        INFO(c["mode"].get<std::string>() << " " << c["bits"].get<int>());
        CHECK(close_bands(coeffs(run("coeff_quantize", {dwt}, p)[0]), 0, c["bands"]) < 1e-10);
    }
    const auto two = coeffs(run("coeff_quantize", {dwt}, {{"bits", "2"}})[0]);
    for (std::size_t b = 1; b < two.channels[0].bands.size(); ++b) {
        const auto& band = two.channels[0].bands[b];
        const double peak = std::ranges::max(band, {}, [](double v) { return std::abs(v); });
        CHECK(std::ranges::all_of(band, [&](double v) { return v == 0 || std::abs(std::abs(v) - std::abs(peak)) < 1e-15; }));
    }
    CHECK(two.channels[0].bands[0] == coeffs(dwt).channels[0].bands[0]);
}

TEST_CASE("wpt matches pywt packets with the core best basis and iwpt restores the signal", "[pipeline][blocks2]") {
    const auto in = golden_input();
    for (const auto& c : golden()["wpt"]) {
        auto signal = in;
        if (c["bands"].size() > 1) signal.channels.push_back(vec(golden()["x2"]));
        const Params p{{"wavelet", c["wavelet"].get<std::string>()}, {"ext", c["mode"].get<std::string>()}, {"levels", std::to_string(c["levels"].get<int>())},
                       {"basis", c["basis"].get<std::string>()}};
        INFO(c["wavelet"].get<std::string>() << " " << c["mode"].get<std::string>() << " " << c["basis"].get<std::string>());
        const auto out = run("wpt", {signal}, p)[0];
        const auto& w = coeffs(out);
        REQUIRE(w.basis.size() == c["paths"].size());
        for (std::size_t k = 0; k < w.basis.size(); ++k) CHECK(band_label(w, k) == c["paths"][k].get<std::string>());
        for (std::size_t ch = 0; ch < signal.channels.size(); ++ch) CHECK(close_bands(w, ch, c["bands"][ch]) < 1e-10);
        const auto back = audio(run("iwpt", {out})[0]);
        REQUIRE(back.channels.size() == signal.channels.size());
        for (std::size_t ch = 0; ch < signal.channels.size(); ++ch) CHECK(close(back.channels[ch], signal.channels[ch]) < 1e-10);
    }
    const auto best = run("wpt", {in})[0];
    const auto& b = coeffs(best);
    const auto label = band_label(b, 1);
    const auto band = band_port(b, label);
    REQUIRE(band);
    CHECK(audio(*band).sample_rate == in.sample_rate / std::pow(2.0, static_cast<double>(label.size())));
    CHECK(audio(*band).channels[0] == b.channels[0].bands[1]);
    const auto report_of = report(run("band_energy", {best})[0]);
    CHECK(report_of.fields.front().first == "c0." + band_label(b, 0));
    std::vector<double> sum(in.channels[0].size(), 0.0);
    for (std::size_t k = 0; k < b.basis.size(); ++k) {
        const auto part = mono(run("mra", {best}, {{"component", band_label(b, k)}})[0]);
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += part[i];
    }
    CHECK(close(sum, in.channels[0]) < 1e-10);
    const auto kept = run("keep_largest", {best}, {{"amount", "50"}})[0];
    CHECK(mono(run("iwpt", {kept})[0]).size() == in.channels[0].size());
    CHECK(error_of("idwt", {best}).find("iwpt") != std::string::npos);
    CHECK(error_of("iswt", {best}).find("iwpt") != std::string::npos);
    CHECK(error_of("iwpt", {dwt_of_input()}).find("idwt") != std::string::npos);
    CHECK(error_of("wpt", {buffer(8000, {{1, 2, 3}})}, {{"levels", "2"}}).find("wpt") != std::string::npos);
    auto broken = b;
    broken.channels[0].bands.pop_back();
    CHECK(error_of("iwpt", {Value{broken}}).find("iwpt") != std::string::npos);
    auto foreign = coeffs(dwt_of_input());
    CHECK(foreign.basis.empty());
    CHECK(value_key(best) != value_key(Value{[&] {
              auto other = b;
              other.basis = core::level_basis(1);
              return other;
          }()}));
}

TEST_CASE("lpc matches solve_toeplitz and lpc_synth inverts it exactly", "[pipeline][blocks2]") {
    const auto in = golden_input();
    for (const auto& c : golden()["lpc"]) {
        const auto order = c["order"].get<int>();
        INFO(order);
        const auto out = run("lpc", {in}, {{"order", std::to_string(order)}});
        const auto& r = report(out[1]);
        CHECK(field(r, "order") == order);
        CHECK(std::abs(field(r, "gain_db") - c["gain_db"].get<double>()) < 1e-10);
        for (int j = 0; j < order; ++j) {
            CHECK(std::abs(field(r, "a" + std::to_string(j + 1)) - c["a"][j].get<double>()) < 1e-10);
            CHECK(std::abs(field(r, "k" + std::to_string(j + 1)) - c["k"][j].get<double>()) < 1e-10);
        }
        CHECK(close(mono(out[0]), vec(c["residual"])) < 1e-10);
        CHECK(close(mono(run("lpc_synth", {out[0], out[1]})[0]), in.channels[0]) < 1e-10);
    }
    auto stereo = in;
    stereo.channels.push_back(vec(golden()["x2"]));
    stereo.channels.push_back(std::vector<double>(7, 0.0));
    const auto out = run("lpc", {stereo}, {{"order", "6"}});
    CHECK(field(report(out[1]), "a1_ch2") == 0);
    CHECK(field(report(out[1]), "gain_db_ch2") == 0);
    const auto back = audio(run("lpc_synth", {out[0], out[1]})[0]);
    for (std::size_t ch = 0; ch < stereo.channels.size(); ++ch) CHECK(close(back.channels[ch], stereo.channels[ch]) < 1e-10);
    CHECK(error_of("lpc_synth", {out[0], Report{}}).find("lpc") != std::string::npos);
    CHECK(error_of("lpc_synth", {golden_input(), out[1]}).find("3") != std::string::npos);
    std::vector<double> tone(64);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = std::cos(0.3 * static_cast<double>(i));
    const auto pure = attempt("lpc", {buffer(8000, {tone})}, {{"order", "64"}});
    if (!pure) CHECK(pure.error().message.find("64") != std::string::npos);
    CHECK(mono(run("lpc", {buffer(8000, {{0.5}})})[0]) == std::vector<double>{0.5});
}

TEST_CASE("shift moves samples with zeros or circularly like numpy.roll", "[pipeline][blocks2]") {
    const auto in = golden_input();
    for (const auto& c : golden()["shift"]) {
        INFO(c["samples"].get<int>() << " " << c["mode"].get<std::string>());
        CHECK(mono(run("shift", {in}, {{"samples", std::to_string(c["samples"].get<int>())}, {"mode", c["mode"].get<std::string>()}})[0]) == vec(c["y"]));
    }
    CHECK(mono(run("shift", {buffer(8, {{1, 2, 3}})}, {{"samples", "-5"}})[0]) == std::vector<double>{0, 0, 0});
    CHECK(mono(run("shift", {buffer(8, {{}})}, {{"samples", "3"}, {"mode", "circular"}})[0]).empty());
}

TEST_CASE("biquad follows the RBJ cookbook through lfilter", "[pipeline][blocks2]") {
    const auto in = golden_input();
    for (const auto& c : golden()["biquad"]) {
        const Params p{{"type", c["type"].get<std::string>()}, {"f0", text(c["f0"].get<double>())}, {"q", text(c["q"].get<double>())}, {"gain_db", text(c["gain_db"].get<double>())}};
        INFO(c["type"].get<std::string>());
        CHECK(close(mono(run("biquad", {in}, p)[0]), vec(c["y"])) < 1e-10);
    }
    CHECK(error_of("biquad", {in}, {{"f0", "4000"}}).find("4000") != std::string::npos);
    CHECK_FALSE(attempt("biquad", {buffer(0, {{1, 2}})}));
}

TEST_CASE("compressor, limiter and gate follow the reference gain computer", "[pipeline][blocks2]") {
    std::vector<double> loud = vec(golden()["x"]);
    for (auto& v : loud) v *= 2.5;
    const auto x2 = vec(golden()["x2"]);
    const auto in = buffer(golden()["fs"].get<double>(), {loud, std::vector<double>(x2.begin(), x2.begin() + 150)});
    for (const auto& c : golden()["dynamics"]) {
        Params p;
        for (const auto& [name, value] : c["params"].items()) p[name] = value.is_string() ? value.get<std::string>() : text(value.get<double>());
        const auto block = c["block"].get<std::string>();
        INFO(block << " " << c["params"].dump());
        const auto out = audio(run(block, {in}, p)[0]);
        REQUIRE(out.channels.size() == 2);
        for (std::size_t ch = 0; ch < 2; ++ch) CHECK(close(out.channels[ch], vec(c["y"][ch])) < 1e-10);
    }
    const auto limited = audio(run("limiter", {in}, {{"threshold_db", "-6"}})[0]);
    for (const auto& ch : limited.channels)
        for (const double v : ch) CHECK(std::abs(v) <= std::pow(10.0, -6.0 / 20) * (1 + 1e-12));
    const auto quiet = audio(run("compressor", {buffer(8000, {std::vector<double>(50, 0.01)})}, {{"attack_ms", "0"}})[0]);
    CHECK(quiet.channels[0] == std::vector<double>(50, 0.01));
    const auto silent = audio(run("gate", {buffer(8000, {std::vector<double>(20, 0.0)})})[0]);
    CHECK(silent.channels[0] == std::vector<double>(20, 0.0));
}

TEST_CASE("echo and comb match lfilter with a delay line", "[pipeline][blocks2]") {
    const auto in = golden_input();
    const auto& e = golden()["echo"];
    const Params p{{"delay_ms", text(e["delay_ms"].get<double>())}, {"feedback", text(e["feedback"].get<double>())}, {"mix", text(e["mix"].get<double>())}};
    CHECK(close(mono(run("echo", {in}, p)[0]), vec(e["y"])) < 1e-10);
    for (const auto& c : golden()["comb"]) {
        const Params q{{"kind", c["kind"].get<std::string>()}, {"delay", std::to_string(c["delay"].get<int>())}, {"gain", text(c["gain"].get<double>())}};
        INFO(c["kind"].get<std::string>());
        CHECK(close(mono(run("comb", {in}, q)[0]), vec(c["y"])) < 1e-10);
    }
    CHECK(error_of("comb", {in}, {{"gain", "1"}}).find("comb") != std::string::npos);
    CHECK(run("comb", {in}, {{"kind", "feedforward"}, {"gain", "1"}}).size() == 1);
    CHECK(error_of("comb", {in}, {{"delay", "0"}}).find("comb") != std::string::npos);
    CHECK(error_of("echo", {buffer(0, {{1, 2}})}).find("echo") != std::string::npos);
}

TEST_CASE("mix sums with gains or crossfades linearly from a to b", "[pipeline][blocks2]") {
    const auto& m = golden()["mix"];
    const auto x2 = vec(golden()["x2"]);
    const auto b = buffer(golden()["fs"].get<double>(), {std::vector<double>(x2.begin(), x2.begin() + 120)});
    const Params p{{"gain_a_db", text(m["gain_a_db"].get<double>())}, {"gain_b_db", text(m["gain_b_db"].get<double>())}};
    CHECK(close(mono(run("mix", {golden_input(), b}, p)[0]), vec(m["sum"])) < 1e-12);
    auto fade = p;
    fade["mode"] = "crossfade";
    fade["at"] = text(m["at"].get<double>());
    fade["crossfade_ms"] = text(m["crossfade_ms"].get<double>());
    CHECK(close(mono(run("mix", {golden_input(), b}, fade)[0]), vec(m["crossfade"])) < 1e-12);
    const auto cut = mono(run("mix", {buffer(1000, {{1, 1, 1, 1}}), buffer(1000, {{2, 2, 2, 2}})}, {{"mode", "crossfade"}, {"at", "0.002"}, {"crossfade_ms", "0"}})[0]);
    CHECK(cut == std::vector<double>{1, 1, 2, 2});
    CHECK(error_of("mix", {golden_input(), buffer(1000, {{1}})}).find("1000") != std::string::npos);
}

TEST_CASE("spectrum_peaks and band_power read the Welch spectrum like scipy", "[pipeline][blocks2]") {
    const auto in = golden_input();
    for (const auto& c : golden()["spectrum_peaks"]) {
        const Params p{{"count", std::to_string(c["count"].get<int>())}, {"segment", std::to_string(c["segment"].get<int>())}, {"prominence_db", text(c["prominence_db"].get<double>())}};
        const auto r = report(run("spectrum_peaks", {in}, p)[0]);
        INFO(c["segment"].get<int>());
        REQUIRE(field(r, "peaks") == static_cast<double>(c["hz"].size()));
        for (std::size_t i = 0; i < c["hz"].size(); ++i) {
            CHECK(std::abs(field(r, "peak" + std::to_string(i + 1) + "_hz") - c["hz"][i].get<double>()) < 1e-9);
            CHECK(std::abs(field(r, "peak" + std::to_string(i + 1) + "_db") - c["db"][i].get<double>()) < 1e-10);
        }
    }
    const auto tones = buffer(golden()["tones_rate"].get<double>(), {vec(golden()["tones"])});
    for (const auto& c : golden()["band_power"]) {
        const bool wide = c["segment"].get<int>() == 64;
        const auto r = report(run("band_power", {wide ? in : tones}, {{"bands", c["bands"].get<std::string>()}, {"segment", std::to_string(c["segment"].get<int>())}})[0]);
        INFO(c["bands"].get<std::string>() << " " << c["segment"].get<int>());
        CHECK(r.fields.size() == c["fields"].size());
        double total = 0;
        for (const auto& [name, value] : c["fields"].items()) {
            INFO(name);
            CHECK(std::abs(field(r, name) - value.get<double>()) < 1e-10);
            total += field(r, name);
        }
        CHECK(std::abs(total - 1) < 1e-12);
    }
    auto stereo = in;
    stereo.channels.push_back(std::vector<double>(100, 0.0));
    const auto r = report(run("band_power", {stereo})[0]);
    CHECK(field(r, "1khz_ch1") == 0);
    CHECK(field(report(run("spectrum_peaks", {stereo})[0]), "peaks_ch1") == 0);
    CHECK(error_of("spectrum_peaks", {buffer(0, {{1, 2, 3}})}).find("spectrum_peaks") != std::string::npos);
}

TEST_CASE("the second batch survives empty, silent, one-sample, stereo, rate-0 and huge inputs", "[pipeline][blocks2]") {
    const std::vector<std::string> ids{"polyphase", "ipolyphase", "wpt", "iwpt", "keep_largest", "coeff_quantize", "shift", "biquad", "compressor", "limiter", "gate",
                                       "echo", "comb", "mix", "lpc", "lpc_synth", "spectrum_peaks", "band_power"};
    const std::vector<core::AudioBuffer> signals{buffer(8000, {}),           buffer(8000, {{}}),        buffer(8000, {{0.0}}), buffer(8000, {{0.5}, {-0.5}}),
                                                 buffer(0, {{1, 2}}),        buffer(8000, {std::vector<double>(64, 1e300)})};
    for (const auto& id : ids) {
        const auto* t = find_block_type(id);
        REQUIRE(t);
        const auto params = resolve_params(*t, {});
        REQUIRE(params);
        for (const auto& s : signals) {
            std::vector<Value> inputs;
            for (const auto& port : t->inputs) {
                if (port.type == PortType::coeffs) {
                    const auto d = attempt("dwt", {s}, {{"levels", "1"}, {"wavelet", "haar"}});
                    inputs.push_back(id == "iwpt" || !d ? Value{Coeffs{}} : (*d)[0]);
                } else if (port.type == PortType::report) {
                    inputs.push_back(Report{});
                } else {
                    inputs.push_back(s);
                }
            }
            RunContext ctx{std::filesystem::temp_directory_path()};
            const auto out = t->run(ctx, inputs, *params);
            INFO(id << " on " << s.channels.size() << " channels at " << s.sample_rate << " Hz");
            if (!out) {
                CHECK_FALSE(out.error().message.empty());
                continue;
            }
            CHECK(out->size() == t->outputs.size());
            bool finite = true;
            for (const auto& v : *out) {
                if (const auto* a = std::get_if<core::AudioBuffer>(&v))
                    for (const auto& c : a->channels)
                        for (const double x : c) finite = finite && std::isfinite(x);
                if (const auto* r = std::get_if<Report>(&v))
                    for (const auto& [k, f] : r->fields) finite = finite && (!std::holds_alternative<double>(f) || std::isfinite(std::get<double>(f)));
            }
            CHECK(finite);
        }
    }
}
