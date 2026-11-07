#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

core::AudioBuffer buffer(double rate, std::vector<std::vector<double>> channels) { return {rate, std::move(channels)}; }

const nlohmann::json& golden() {
    static const auto g = signals::test::load_golden("blocks_fx.json");
    return g;
}

std::vector<double> vec(const nlohmann::json& j) { return j.get<std::vector<double>>(); }

double fs() { return golden()["fs"].get<double>(); }
core::AudioBuffer input(const char* name = "x") { return buffer(fs(), {vec(golden()[name])}); }

std::string number(const nlohmann::json& j) {
    std::ostringstream out;
    out.precision(17);
    out << j.get<double>();
    return out.str();
}

double close(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    return core::max_abs_error(a, b);
}

}  // namespace

TEST_CASE("tremolo, modulated_delay, phaser and reverb follow their reference equations", "[pipeline][blocks][fx]") {
    const auto x = input();
    for (const auto& c : golden()["tremolo"]) {
        INFO(c["shape"].get<std::string>());
        const Params p{{"shape", c["shape"].get<std::string>()}, {"rate_hz", number(c["rate"])}, {"depth", number(c["depth"])}};
        CHECK(close(mono(run("tremolo", {x}, p)[0]), vec(c["y"])) < 1e-12);
    }
    for (const auto& c : golden()["modulated_delay"]) {
        INFO(c["mode"].get<std::string>() << " " << c["feedback"].get<double>());
        const Params p{{"mode", c["mode"].get<std::string>()}, {"rate_hz", number(c["rate"])},     {"depth_ms", number(c["depth_ms"])},
                       {"base_ms", number(c["base_ms"])},     {"feedback", number(c["feedback"])}, {"mix", number(c["mix"])}};
        CHECK(close(mono(run("modulated_delay", {x}, p)[0]), vec(c["y"])) < 1e-12);
    }
    for (const auto& c : golden()["phaser"]) {
        INFO(c["stages"].get<int>());
        const Params p{{"stages", std::to_string(c["stages"].get<int>())}, {"rate_hz", number(c["rate"])},     {"min_hz", number(c["min_hz"])},
                       {"max_hz", number(c["max_hz"])},                    {"feedback", number(c["feedback"])}, {"mix", number(c["mix"])}};
        CHECK(close(mono(run("phaser", {x}, p)[0]), vec(c["y"])) < 1e-12);
    }
    for (const auto& c : golden()["reverb"]) {
        INFO(c["room_size"].get<double>());
        const Params p{{"room_size", number(c["room_size"])}, {"damping", number(c["damping"])}, {"wet", number(c["wet"])}, {"dry", number(c["dry"])}};
        CHECK(close(mono(run("reverb", {x}, p)[0]), vec(c["y"])) < 1e-12);
    }
    const auto vibrato = mono(run("modulated_delay", {x}, {{"mode", "vibrato"}, {"mix", "0"}})[0]);
    CHECK(vibrato == mono(run("modulated_delay", {x}, {{"mode", "vibrato"}, {"mix", "1"}})[0]));
    CHECK(mono(run("modulated_delay", {x}, {{"mix", "0"}})[0]) == x.channels[0]);
    CHECK(mono(run("tremolo", {x}, {{"depth", "0"}})[0]) == x.channels[0]);
    CHECK(mono(run("reverb", {x}, {{"wet", "0"}, {"dry", "1"}})[0]) == x.channels[0]);
    CHECK(error_of("phaser", {x}, {{"max_hz", "4000"}}).find("4000") != std::string::npos);
    CHECK(error_of("tremolo", {buffer(0, {{1, 2}})}).find("tremolo") != std::string::npos);
    auto stereo = x;
    stereo.channels.push_back(vec(golden()["r"]));
    const auto both = audio(run("reverb", {stereo})[0]);
    CHECK(both.channels[0] == mono(run("reverb", {x})[0]));
    CHECK(both.channels[1] == mono(run("reverb", {input("r")})[0]));
}

TEST_CASE("wavefold, am and fm match numpy", "[pipeline][blocks][fx]") {
    const auto x = input();
    for (const auto* shape : {"triangle", "sine"}) {
        INFO(shape);
        const auto y = mono(run("wavefold", {x}, {{"drive", "3"}, {"shape", shape}})[0]);
        CHECK(close(y, vec(golden()["wavefold"][shape])) < 1e-12);
        CHECK(std::ranges::all_of(y, [](double v) { return std::abs(v) <= 1; }));
    }
    CHECK(mono(run("wavefold", {buffer(8, {{0.25, -0.5}})}, {{"drive", "1"}})[0]) == std::vector<double>{0.25, -0.5});
    CHECK(close(mono(run("am", {x}, {{"carrier_hz", "1200"}, {"depth", "0.6"}})[0]), vec(golden()["am"])) < 1e-12);
    CHECK(close(mono(run("fm", {x}, {{"carrier_hz", "1200"}, {"deviation_hz", "150"}})[0]), vec(golden()["fm"])) < 1e-10);
    const auto carrier = mono(run("fm", {buffer(fs(), {std::vector<double>(16, 0.0)})}, {{"carrier_hz", "1000"}})[0]);
    CHECK(std::abs(carrier[2] - std::cos(2 * std::numbers::pi * 1000 * 2 / fs())) < 1e-15);
    CHECK(error_of("fm", {buffer(0, {{1}})}).find("fm") != std::string::npos);
}

TEST_CASE("gausspulse and impulse_train sources match scipy and numpy", "[pipeline][blocks][fx]") {
    for (const auto& c : golden()["gausspulse"]) {
        INFO(c["fc"].get<double>());
        const Params p{{"fc", number(c["fc"])}, {"bw", number(c["bw"])},  {"bwr", number(c["bwr"])},
                       {"amplitude", "0.8"},    {"rate", "8000"},         {"duration", "0.040125"}};
        CHECK(close(mono(run("gausspulse", {}, p)[0]), vec(c["y"])) < 1e-12);
    }
    CHECK(mono(run("gausspulse", {}, {{"duration", "0"}})[0]).empty());
    const auto train = mono(run("impulse_train", {}, {{"period", "33"}, {"offset", "7"}, {"amplitude", "0.5"}, {"rate", "8000"}, {"duration", "0.05"}})[0]);
    CHECK(train == vec(golden()["impulse_train"]));
    const auto timed = mono(run("impulse_train", {}, {{"period", "0.001s"}, {"rate", "4000"}, {"duration", "0.003"}})[0]);
    CHECK(timed == std::vector<double>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    CHECK(error_of("impulse_train", {}, {{"period", "0"}}).find("period") != std::string::npos);
}

TEST_CASE("affine, math and binary match numpy and refuse values outside the domain", "[pipeline][blocks][fx]") {
    const auto x = input();
    CHECK(close(mono(run("affine", {x}, {{"scale", "-1.5"}, {"offset", "0.25"}})[0]), vec(golden()["affine"])) < 1e-15);
    const auto positive = input("positive");
    for (const auto& [op, y] : golden()["math"].items()) {
        INFO(op);
        const auto& in = op == "exp" ? x : op == "sign" ? input("sign_input") : positive;
        CHECK(close(mono(run("math", {in}, {{"op", op}, {"p", "2.5"}})[0]), vec(y)) < 1e-12);
    }
    const auto bad = buffer(8, {{1, 2}, {3, -0.5, 4}});
    const auto log_error = error_of("math", {bad}, {{"op", "log"}});
    CHECK(log_error.find("log") != std::string::npos);
    CHECK(log_error.find("-0.5") != std::string::npos);
    CHECK(log_error.find(" 1 ") != std::string::npos);
    CHECK_FALSE(attempt("math", {buffer(8, {{0.0}})}, {{"op", "log10"}}));
    CHECK_FALSE(attempt("math", {buffer(8, {{-1.0}})}, {{"op", "sqrt"}}));
    CHECK_FALSE(attempt("math", {buffer(8, {{0.0}})}, {{"op", "reciprocal"}}));
    CHECK_FALSE(attempt("math", {buffer(8, {{-2.0}})}, {{"op", "pow"}, {"p", "0.5"}}));
    CHECK_FALSE(attempt("math", {buffer(8, {{0.0}})}, {{"op", "pow"}, {"p", "-1"}}));
    CHECK(mono(run("math", {buffer(8, {{-2.0}})}, {{"op", "pow"}, {"p", "3"}})[0]) == std::vector<double>{-8});
    CHECK(error_of("math", {buffer(8, {{800.0}})}, {{"op", "exp"}}).find("math") != std::string::npos);

    const auto r = input("r");
    auto shifted_r = r;
    for (auto& v : shifted_r.channels[0]) v = std::abs(v) + 0.05;
    const auto short_r = buffer(fs(), {std::vector<double>(r.channels[0].begin(), r.channels[0].begin() + 300)});
    const auto& b = golden()["binary"];
    CHECK(close(mono(run("binary", {x, shifted_r}, {{"op", "divide"}})[0]), vec(b["divide"])) < 1e-12);
    for (const auto* op : {"min", "max", "hypot"}) {
        INFO(op);
        CHECK(close(mono(run("binary", {x, short_r}, {{"op", op}})[0]), vec(b[op])) < 1e-12);
    }
    CHECK(close(mono(run("binary", {positive, input("exponent")}, {{"op", "pow"}})[0]), vec(b["pow"])) < 1e-12);
    CHECK(error_of("binary", {x, short_r}, {{"op", "divide"}}).find("300") != std::string::npos);
    CHECK(error_of("binary", {x, buffer(16000, {{1}})}).find("16000") != std::string::npos);
    const auto broadcast = audio(run("binary", {buffer(8, {{1, 2}, {3, 4}}), buffer(8, {{2, 2}})}, {{"op", "max"}})[0]);
    CHECK(broadcast.channels == std::vector<std::vector<double>>{{2, 2}, {3, 4}});
}

TEST_CASE("repeat, select_channel, channel_delay, stereo_width, balance, sample_hold and dc_block", "[pipeline][blocks][fx]") {
    const auto x = input();
    const auto r = input("r");
    auto head = x;
    head.channels[0].resize(50);
    CHECK(mono(run("repeat", {head}, {{"times", "3"}})[0]) == vec(golden()["repeat"]));
    const auto stereo = buffer(8, {{1, 2, 3}, {4, 5, 6}});
    CHECK(audio(run("select_channel", {stereo}, {{"index", "1"}})[0]).channels == std::vector<std::vector<double>>{{4, 5, 6}});
    CHECK(error_of("select_channel", {stereo}, {{"index", "2"}}).find("2") != std::string::npos);
    CHECK_FALSE(attempt("select_channel", {buffer(8, {})}));

    const auto three = buffer(fs(), {x.channels[0], r.channels[0], x.channels[0]});
    const auto delayed = audio(run("channel_delay", {three}, {{"delays", "0, 12 -5"}})[0]);
    for (std::size_t ch = 0; ch < 3; ++ch) CHECK(delayed.channels[ch] == vec(golden()["channel_delay"][ch]));
    CHECK(audio(run("channel_delay", {stereo}, {{"delays", "1"}})[0]).channels == std::vector<std::vector<double>>{{0, 1, 2}, {0, 4, 5}});
    CHECK(error_of("channel_delay", {stereo}, {{"delays", "1.5"}}).find("1.5") != std::string::npos);
    CHECK_FALSE(attempt("channel_delay", {stereo}, {{"delays", "one"}}));

    const auto pair = buffer(fs(), {x.channels[0], r.channels[0]});
    const auto wide = audio(run("stereo_width", {pair}, {{"width", "0.4"}})[0]);
    CHECK(close(wide.channels[0], vec(golden()["stereo_width"][0])) < 1e-15);
    CHECK(close(wide.channels[1], vec(golden()["stereo_width"][1])) < 1e-15);
    const auto narrow = audio(run("stereo_width", {pair}, {{"width", "0"}})[0]);
    CHECK(narrow.channels[0] == narrow.channels[1]);
    CHECK(error_of("stereo_width", {x}).find("1") != std::string::npos);
    const auto balanced = audio(run("balance", {pair}, {{"position", "0.6"}})[0]);
    CHECK(close(balanced.channels[0], vec(golden()["balance"][0])) == 0);
    CHECK(close(balanced.channels[1], vec(golden()["balance"][1])) == 0);
    CHECK(audio(run("balance", {pair})[0]).channels == pair.channels);
    CHECK_FALSE(attempt("balance", {x}));

    CHECK(mono(run("sample_hold", {x}, {{"hold", "5"}})[0]) == vec(golden()["sample_hold"]));
    CHECK(mono(run("sample_hold", {buffer(1000, {{1, 2, 3, 4, 5}})}, {{"hold", "0.002s"}})[0]) == std::vector<double>{1, 1, 3, 3, 5});
    CHECK(error_of("sample_hold", {x}, {{"hold", "0"}}).find("hold") != std::string::npos);

    auto offset = x;
    for (auto& v : offset.channels[0]) v += 0.5;
    CHECK(close(mono(run("dc_block", {offset}, {{"r", "0.99"}})[0]), vec(golden()["dc_block"])) < 1e-12);
    const auto settled = mono(run("dc_block", {buffer(8, {std::vector<double>(4000, 1.0)})}, {{"r", "0.99"}})[0]);
    CHECK(std::abs(settled.back()) < 1e-12);
}

TEST_CASE("the fx and utility blocks survive empty, silent, one-sample and rate-0 inputs", "[pipeline][blocks][fx]") {
    const std::vector<std::string> ids{"tremolo",  "modulated_delay", "phaser",       "reverb",        "wavefold",       "am",       "fm",           "gausspulse",
                                       "impulse_train", "affine",     "math",         "binary",        "repeat",         "select_channel", "channel_delay", "stereo_width",
                                       "balance",  "sample_hold",     "dc_block"};
    const std::vector<std::string> timed{"tremolo", "modulated_delay", "phaser", "reverb", "am", "fm"};
    const std::vector<core::AudioBuffer> signals{buffer(8000, {}), buffer(8000, {{}}), buffer(8000, {{0.0}}), buffer(8000, {{0.5}, {-0.5}}), buffer(0, {{1, 2}})};
    for (const auto& id : ids) {
        const auto* t = find_block_type(id);
        REQUIRE(t);
        const auto params = resolve_params(*t, {});
        REQUIRE(params);
        for (const auto& s : signals) {
            std::vector<Value> inputs(t->inputs.size(), Value{s});
            RunContext ctx{std::filesystem::temp_directory_path()};
            const auto out = t->run(ctx, inputs, *params);
            INFO(id << " on " << s.channels.size() << " channels at " << s.sample_rate << " Hz");
            if (!t->inputs.empty() && s.sample_rate == 0 && std::ranges::find(timed, id) != timed.end()) CHECK_FALSE(out);
            if (!out) {
                CHECK_FALSE(out.error().message.empty());
                continue;
            }
            REQUIRE(out->size() == 1);
            const auto& a = audio((*out)[0]);
            for (const auto& c : a.channels) CHECK(std::ranges::all_of(c, [](double v) { return std::isfinite(v); }));
            if (!t->inputs.empty() && id != "select_channel") CHECK(a.channels.size() == s.channels.size());
        }
    }
}
