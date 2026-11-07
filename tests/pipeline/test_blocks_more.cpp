#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/lifting.hpp"
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

core::AudioBuffer buffer(double rate, std::vector<std::vector<double>> channels) { return {rate, std::move(channels)}; }

const nlohmann::json& golden() {
    static const auto g = signals::test::load_golden("blocks.json");
    return g;
}

core::AudioBuffer golden_input() { return buffer(golden()["fs"].get<double>(), {golden()["x"].get<std::vector<double>>()}); }

std::vector<double> vec(const nlohmann::json& j) { return j.get<std::vector<double>>(); }

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

}  // namespace

TEST_CASE("pad, concat, fade and trim_silence shape the time axis", "[pipeline][blocks]") {
    const auto stereo = buffer(1000, {{1, 2, 3}, {4, 5, 6}});
    const auto padded = audio(run("pad", {stereo}, {{"before", "2"}, {"after", "0.001s"}})[0]);
    CHECK(padded.channels[0] == std::vector<double>{0, 0, 1, 2, 3, 0});
    CHECK(padded.channels[1] == std::vector<double>{0, 0, 4, 5, 6, 0});
    const auto joined = audio(run("concat", {stereo, buffer(1000, {{7}})})[0]);
    CHECK(joined.channels[0] == std::vector<double>{1, 2, 3, 7});
    CHECK(joined.channels[1] == std::vector<double>{4, 5, 6, 7});
    CHECK(error_of("concat", {stereo, buffer(2000, {{7}})}).find("2000") != std::string::npos);
    const auto ones = buffer(1000, {std::vector<double>(6, 1.0)});
    CHECK(mono(run("fade", {ones}, {{"fade_in", "4"}, {"fade_out", "2"}})[0]) == std::vector<double>{0, 0.25, 0.5, 0.75, 0.5, 0});
    const auto cosine = mono(run("fade", {ones}, {{"fade_in", "2"}, {"fade_out", "0"}, {"curve", "cosine"}})[0]);
    CHECK(std::abs(cosine[1] - 0.5) < 1e-15);
    CHECK(cosine[0] == 0);
    const auto quiet = buffer(1000, {{0.0001, 0.5, 0, -0.2, 0.0001}, {0, 0, 0.1, 0, 0.002}});
    const auto trimmed = audio(run("trim_silence", {quiet}, {{"threshold_db", "-40"}})[0]);
    CHECK(trimmed.channels[0] == std::vector<double>{0.5, 0, -0.2});
    CHECK(trimmed.channels[1] == std::vector<double>{0, 0.1, 0});
    CHECK(mono(run("trim_silence", {buffer(1000, {{0, 0}})})[0]).empty());
}

TEST_CASE("multiply takes a second signal or a constant", "[pipeline][blocks]") {
    const auto a = buffer(8, {{1, 2, 3}, {-1, -2, -3}});
    CHECK(audio(run("multiply", {a, Value{}}, {{"factor", "2"}})[0]).channels[1] == std::vector<double>{-2, -4, -6});
    const auto product = audio(run("multiply", {a, buffer(8, {{2, 0.5}})})[0]);
    CHECK(product.channels[0] == std::vector<double>{2, 1, 0});
    CHECK(product.channels[1] == std::vector<double>{-2, -1, 0});
    CHECK_FALSE(attempt("multiply", {a, buffer(16, {{1}})}));
    CHECK(find_block_type("multiply")->inputs[1].optional);
}

TEST_CASE("window matches scipy get_window with fftbins off", "[pipeline][blocks]") {
    for (const auto& w : golden()["windows"]) {
        const auto kind = w["kind"].get<std::string>();
        Params p{{"kind", kind}};
        if (kind == "kaiser") p["beta"] = std::to_string(w["extra"].get<double>());
        if (kind == "tukey") p["alpha"] = std::to_string(w["extra"].get<double>());
        INFO(kind);
        CHECK(close(mono(run("window", {buffer(8, {std::vector<double>(37, 1.0)})}, p)[0]), vec(w["w"])) < 1e-13);
    }
    CHECK(mono(run("window", {buffer(8, {{0.5}})})[0]) == std::vector<double>{0.5});
}

TEST_CASE("sample-wise blocks follow numpy", "[pipeline][blocks]") {
    const auto x = buffer(8, {{-2, 0.5, 3}});
    CHECK(mono(run("invert", {x})[0]) == std::vector<double>{2, -0.5, -3});
    CHECK(mono(run("abs", {x})[0]) == std::vector<double>{2, 0.5, 3});
    CHECK(mono(run("square", {x})[0]) == std::vector<double>{4, 0.25, 9});
    CHECK(mono(run("dc_offset", {x}, {{"value", "1"}})[0]) == std::vector<double>{-1, 1.5, 4});
    const auto in = golden_input();
    CHECK(close(mono(run("cumsum", {in})[0]), vec(golden()["cumsum"])) < 1e-12);
    CHECK(close(mono(run("diff", {in})[0]), vec(golden()["diff"])) < 1e-15);
    CHECK(close(mono(run("softclip", {in}, {{"drive", "2"}})[0]), vec(golden()["softclip"])) < 1e-15);
    CHECK(close(mono(run("cumsum", {run("diff", {in})[0]})[0]), in.channels[0]) < 1e-12);
    CHECK(error_of("square", {buffer(8, {{1e200}})}).find("square") != std::string::npos);
}

TEST_CASE("add_noise hits the requested SNR and repeats with its seed", "[pipeline][blocks]") {
    auto in = golden_input();
    in.channels.push_back(in.channels[0]);
    for (const auto* color : {"white", "pink", "brown"}) {
        const auto noisy = audio(run("add_noise", {in}, {{"color", color}, {"snr_db", "12"}, {"seed", "7"}})[0]);
        INFO(color);
        double noise = 0;
        for (const auto& c : noisy.channels)
            for (std::size_t i = 0; i < c.size(); ++i) noise += (c[i] - in.channels[0][i]) * (c[i] - in.channels[0][i]);
        const double signal = 2 * core::energy(in.channels[0]);
        CHECK(std::abs(10 * std::log10(signal / noise) - 12) < 1e-9);
        CHECK(noisy.channels[0] != noisy.channels[1]);
        CHECK(audio(run("add_noise", {in}, {{"color", color}, {"snr_db", "12"}, {"seed", "7"}})[0]).channels == noisy.channels);
    }
    CHECK(error_of("add_noise", {buffer(8, {{0, 0, 0}})}).find("add_noise") != std::string::npos);
}

TEST_CASE("chirp and osc sources match scipy", "[pipeline][blocks]") {
    for (const auto& c : golden()["chirp"]) {
        const auto method = c["method"].get<std::string>() == "linear" ? "linear" : "log";
        const auto y = mono(run("chirp", {}, {{"method", method}, {"f0", "100"}, {"f1", "3000"}, {"rate", "8000"}, {"duration", "0.032"}})[0]);
        INFO(method);
        CHECK(close(y, vec(c["y"])) < 1e-9);
    }
    CHECK_FALSE(attempt("chirp", {}, {{"method", "log"}, {"f0", "0"}}));
    for (const auto& o : golden()["osc"]) {
        const auto kind = o["kind"].get<std::string>();
        const Params p{{"kind", kind}, {"freq", "220"}, {"duty", std::to_string(o["duty"].get<double>())}, {"width", std::to_string(o["width"].get<double>())},
                       {"rate", "8000"}, {"duration", "0.032"}};
        INFO(kind << " " << o["width"].get<double>());
        CHECK(close(mono(run("osc", {}, p)[0]), vec(o["y"])) < 1e-12);
    }
    const auto pulses = mono(run("osc", {}, {{"kind", "pulses"}, {"freq", "2000"}, {"rate", "8000"}, {"duration", "0.001"}, {"amplitude", "0.5"}})[0]);
    CHECK(pulses == std::vector<double>{0.5, 0, 0, 0, 0.5, 0, 0, 0});
}

TEST_CASE("iir designs a cascade and filters like sosfilt and sosfiltfilt", "[pipeline][blocks]") {
    const auto in = golden_input();
    for (const auto& c : golden()["iir"]) {
        std::string cutoff;
        for (const auto& f : c["cutoff"]) cutoff += (cutoff.empty() ? "" : ", ") + std::to_string(f.get<double>());
        Params p{{"family", c["family"].get<std::string>()}, {"type", c["type"].get<std::string>()}, {"order", std::to_string(c["order"].get<int>())},
                 {"cutoff", cutoff}, {"rp", std::to_string(c["rp"].get<double>())}, {"rs", std::to_string(c["rs"].get<double>())}};
        INFO(c["family"].get<std::string>() << " " << c["type"].get<std::string>());
        CHECK(close(mono(run("iir", {in}, p)[0]), vec(c["y"])) < 1e-10);
        p["zero_phase"] = "yes";
        CHECK(close(mono(run("iir", {in}, p)[0]), vec(c["zero_phase"])) < 1e-10);
    }
    CHECK(error_of("iir", {in}, {{"cutoff", "4000"}}).find("4000") != std::string::npos);
    CHECK(error_of("iir", {in}, {{"type", "bandpass"}, {"cutoff", "1000"}}).find("1") != std::string::npos);
    CHECK_FALSE(attempt("iir", {in}, {{"cutoff", "1000 Hz"}}));
    CHECK_FALSE(attempt("iir", {buffer(8000, {std::vector<double>(15, 1.0)})}, {{"zero_phase", "yes"}}));
}

TEST_CASE("fir, median, moving_average and emphasis match scipy", "[pipeline][blocks]") {
    const auto in = golden_input();
    for (const auto& c : golden()["fir"]) {
        std::string cutoff;
        for (const auto& f : c["cutoff"]) cutoff += (cutoff.empty() ? "" : ", ") + std::to_string(f.get<double>());
        Params p{{"numtaps", std::to_string(c["numtaps"].get<int>())}, {"cutoff", cutoff}, {"window", c["window"].get<std::string>()},
                 {"beta", std::to_string(c["beta"].get<double>())}, {"pass_zero", c["pass_zero"].get<bool>() ? "yes" : "no"}};
        INFO(c["numtaps"].get<int>());
        CHECK(close(mono(run("fir", {in}, p)[0]), vec(c["y"])) < 1e-12);
        p["compensate"] = "yes";
        CHECK(close(mono(run("fir", {in}, p)[0]), vec(c["centred"])) < 1e-12);
    }
    CHECK_FALSE(attempt("fir", {in}, {{"numtaps", "30"}, {"pass_zero", "no"}}));
    for (const auto& c : golden()["median"])
        CHECK(close(mono(run("median", {in}, {{"kernel", std::to_string(c["kernel"].get<int>())}})[0]), vec(c["y"])) == 0);
    CHECK(error_of("median", {in}, {{"kernel", "4"}}).find("4") != std::string::npos);
    for (const auto& c : golden()["moving_average"]) {
        const Params p{{"length", std::to_string(c["length"].get<int>())}, {"centred", c["centred"].get<bool>() ? "yes" : "no"}};
        CHECK(close(mono(run("moving_average", {in}, p)[0]), vec(c["y"])) < 1e-12);
    }
    CHECK(close(mono(run("preemphasis", {in})[0]), vec(golden()["preemphasis"])) < 1e-15);
    CHECK(close(mono(run("deemphasis", {in}, {{"coeff", "0.9"}})[0]), vec(golden()["deemphasis"])) < 1e-12);
    const auto back = mono(run("deemphasis", {run("preemphasis", {in}, {{"coeff", "0.9"}})[0]}, {{"coeff", "0.9"}})[0]);
    CHECK(close(back, in.channels[0]) < 1e-12);
}

TEST_CASE("lift97 levels and ilift97 restore the signal", "[pipeline][blocks]") {
    auto in = golden_input();
    in.channels.push_back(std::vector<double>(in.channels[0].begin(), in.channels[0].begin() + 77));
    const auto c = std::get<Coeffs>(run("lift97", {in}, {{"levels", "3"}})[0]);
    REQUIRE(c.channels.size() == 2);
    CHECK(c.channels[0].bands.size() == 4);
    const auto first = core::lift97(in.channels[0]);
    CHECK(c.channels[0].bands.back() == first.d);
    const auto back = audio(run("ilift97", {Value{c}})[0]);
    CHECK(close(back.channels[0], in.channels[0]) < 1e-12);
    CHECK(close(back.channels[1], in.channels[1]) < 1e-12);
    CHECK(error_of("ilift53", {Value{c}}).find("ilift97") != std::string::npos);
    CHECK(error_of("ilift97", {run("lift53", {in})[0]}).find("ilift53") != std::string::npos);
    CHECK_FALSE(attempt("lift97", {buffer(8, {{1, 2, 3}})}, {{"levels", "2"}}));
}

TEST_CASE("mra rebuilds one band like pywt.mra and the bands add up", "[pipeline][blocks]") {
    const auto in = golden_input();
    const auto dwt = run("dwt", {in}, {{"wavelet", "db2"}, {"levels", "3"}})[0];
    const std::vector<std::string> names{"a", "d3", "d2", "d1"};
    std::vector<double> sum(in.channels[0].size(), 0.0);
    for (std::size_t k = 0; k < names.size(); ++k) {
        const auto y = mono(run("mra", {dwt}, {{"component", names[k]}})[0]);
        INFO(names[k]);
        CHECK(close(y, vec(golden()["mra_dwt"][k])) < 1e-12);
        for (std::size_t i = 0; i < y.size(); ++i) sum[i] += y[i];
    }
    CHECK(close(sum, in.channels[0]) < 1e-12);
    const auto short_in = buffer(in.sample_rate, {std::vector<double>(in.channels[0].begin(), in.channels[0].begin() + 192)});
    const auto swt = run("swt", {short_in}, {{"wavelet", "db2"}, {"levels", "3"}, {"norm", "yes"}})[0];
    for (std::size_t k = 0; k < names.size(); ++k) CHECK(close(mono(run("mra", {swt}, {{"component", names[k]}})[0]), vec(golden()["mra_swt"][k])) < 1e-12);
    const auto lifted = run("lift53", {in}, {{"levels", "2"}})[0];
    const auto a = mono(run("mra", {lifted}, {{"component", "a"}})[0]);
    const auto d1 = mono(run("mra", {lifted}, {{"component", "d1"}})[0]);
    const auto d2 = mono(run("mra", {lifted}, {{"component", "d2"}})[0]);
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(std::abs(a[i] + d1[i] + d2[i] - in.channels[0][i]) < 1e-12);
    CHECK(error_of("mra", {dwt}, {{"component", "d4"}}).find("d3") != std::string::npos);
}

TEST_CASE("channel blocks merge, swap, pan and duplicate", "[pipeline][blocks]") {
    const auto l = buffer(8, {{1, 2, 3}});
    const auto r = buffer(8, {{4, 5}});
    const auto stereo = audio(run("merge", {l, r})[0]);
    CHECK(stereo.channels == std::vector<std::vector<double>>{{1, 2, 3}, {4, 5, 0}});
    CHECK_FALSE(attempt("merge", {l, buffer(16, {{1}})}));
    CHECK(audio(run("swap", {stereo})[0]).channels[0] == std::vector<double>{4, 5, 0});
    CHECK_FALSE(attempt("swap", {l}));
    const auto centre = audio(run("pan", {l})[0]);
    CHECK(std::abs(centre.channels[0][0] - std::sqrt(0.5)) < 1e-15);
    CHECK(std::abs(centre.channels[1][2] - 3 * std::sqrt(0.5)) < 1e-15);
    const auto hard_left = audio(run("pan", {l}, {{"position", "-1"}, {"law", "linear"}})[0]);
    CHECK(hard_left.channels == std::vector<std::vector<double>>{{1, 2, 3}, {0, 0, 0}});
    const auto four = audio(run("duplicate", {stereo}, {{"channels", "4"}})[0]);
    CHECK(four.channels.size() == 4);
    CHECK(four.channels[2] == stereo.channels[0]);
    CHECK(four.channels[3] == stereo.channels[1]);
}

TEST_CASE("stats and loudness report measurements", "[pipeline][blocks]") {
    const auto in = golden_input();
    const auto r = std::get<Report>(run("stats", {in})[0]);
    for (const auto& [name, value] : golden()["stats"].items()) {
        INFO(name);
        CHECK(std::abs(field(r, name) - value.get<double>()) < 1e-9 * std::max(1.0, std::abs(value.get<double>())));
    }
    auto two = in;
    two.channels.push_back(std::vector<double>(10, 0.0));
    const auto both = std::get<Report>(run("stats", {two})[0]);
    CHECK(field(both, "rms_ch0") == field(r, "rms"));
    CHECK(field(both, "crest_db_ch1") == 0);
    CHECK(field(both, "centroid_hz_ch1") == 0);
    std::vector<double> sine(48000);
    for (std::size_t n = 0; n < sine.size(); ++n) sine[n] = std::sin(2 * std::numbers::pi * 1000 * static_cast<double>(n) / 48000);
    const auto loud = std::get<Report>(run("loudness", {buffer(48000, {sine})})[0]);
    CHECK(field(loud, "windows") == 7);
    CHECK(std::abs(field(loud, "max_dbfs") + 10 * std::log10(2.0)) < 1e-9);
    CHECK(std::abs(field(loud, "mean_dbfs") + 10 * std::log10(2.0)) < 1e-9);
    CHECK(field(std::get<Report>(run("loudness", {buffer(48000, {std::vector<double>(100, 0.0)})})[0]), "max_dbfs") == -200);
    CHECK_FALSE(attempt("loudness", {buffer(48000, {sine})}, {{"hop", "0"}}));
}

TEST_CASE("the new blocks survive empty, silent and one-sample inputs", "[pipeline][blocks]") {
    const std::vector<std::string> ids{"pad", "concat", "multiply", "window", "fade", "invert", "abs", "square", "diff", "cumsum", "softclip", "dc_offset",
                                       "trim_silence", "add_noise", "chirp", "osc", "iir", "fir", "median", "moving_average", "preemphasis", "deemphasis",
                                       "lift97", "ilift97", "mra", "merge", "swap", "pan", "duplicate", "stats", "loudness"};
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
            INFO(id << " on " << s.channels.size() << " channels");
            if (!out) {
                CHECK_FALSE(out.error().message.empty());
                continue;
            }
            CHECK(out->size() == t->outputs.size());
            bool finite = true;
            for (const auto& v : *out)
                if (const auto* a = std::get_if<core::AudioBuffer>(&v))
                    for (const auto& c : a->channels)
                        for (const double x : c) finite = finite && std::isfinite(x);
            CHECK(finite);
        }
    }
}
