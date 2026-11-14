#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "python/form.hpp"

namespace signals::python {
namespace {

using pipeline::param_int;
using pipeline::param_real;
using pipeline::param_text;

std::string real(Emitter& em, const NodeIo& io, std::string_view name) { return em.number(param_real(io.node.params, name)); }

double rate_param(const NodeIo& io) { return static_cast<double>(param_int(io.node.params, "rate")); }

std::size_t source_length(const NodeIo& io) { return static_cast<std::size_t>(std::llround(param_real(io.node.params, "duration") * rate_param(io))); }

core::Result<std::string> tremolo(const NodeIo& io, Emitter& em) {
    em.function("lfo");
    return fmt::format("{0} = {1} * (1 - {2} * (1 - lfo({3}, 2 * np.pi * {4} * np.arange({1}.shape[1]) / {5})) / 2)", io.out[0], io.in[0], real(em, io, "depth"),
                       py_str(param_text(io.node.params, "shape")), real(em, io, "rate_hz"), io.in_fs[0]);
}

core::Result<std::string> modulated_delay(const NodeIo& io, Emitter& em) {
    const auto& mode = param_text(io.node.params, "mode");
    em.function("modulated_delay");
    return fmt::format("{} = modulated_delay({}, {}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "rate_hz"), real(em, io, "depth_ms"), real(em, io, "base_ms"),
                       mode == "flanger" ? real(em, io, "feedback") : std::string{"0.0"}, mode == "vibrato" ? std::string{"1.0"} : real(em, io, "mix"));
}

core::Result<std::string> phaser(const NodeIo& io, Emitter& em) {
    em.function("phaser");
    return fmt::format("{} = phaser({}, {}, {}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], param_int(io.node.params, "stages"), real(em, io, "rate_hz"),
                       real(em, io, "min_hz"), real(em, io, "max_hz"), real(em, io, "feedback"), real(em, io, "mix"));
}

core::Result<std::string> reverb(const NodeIo& io, Emitter& em) {
    em.function("reverb");
    return fmt::format("{} = reverb({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "room_size"), real(em, io, "damping"), real(em, io, "wet"),
                       real(em, io, "dry"));
}

core::Result<std::string> wavefold(const NodeIo& io, Emitter& em) {
    const auto u = fmt::format("{} * {}", real(em, io, "drive"), io.in[0]);
    if (param_text(io.node.params, "shape") == "sine") return fmt::format("{} = np.sin(np.pi / 2 * ({}))", io.out[0], u);
    return fmt::format("{0}_v = np.fmod({1} + 1, 4.0)\n{0}_v = np.where({0}_v < 0, {0}_v + 4, {0}_v)\n{0} = np.where({0}_v < 2, {0}_v - 1, 3 - {0}_v)", io.out[0], u);
}

core::Result<std::string> am(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = (1 + {1} * {2}) * np.cos(2 * np.pi * {3} * np.arange({2}.shape[1]) / {4})", io.out[0], real(em, io, "depth"), io.in[0], real(em, io, "carrier_hz"),
                       io.in_fs[0]);
}

core::Result<std::string> fm(const NodeIo& io, Emitter& em) {
    return fmt::format("{0}_integral = np.concatenate([np.zeros(({1}.shape[0], 1)), np.cumsum({1}, axis=1)[:, :-1]], axis=1)\n"
                       "{0} = np.cos(2 * np.pi * {2} * np.arange({1}.shape[1]) / {4} + 2 * np.pi * {3} * {0}_integral / {4})",
                       io.out[0], io.in[0], real(em, io, "carrier_hz"), real(em, io, "deviation_hz"), io.in_fs[0]);
}

core::Result<std::string> gausspulse(const NodeIo& io, Emitter& em) {
    const auto n = source_length(io);
    const auto fs = em.number(rate_param(io));
    return fmt::format("{0}_a = -(np.pi * {1} * {2}) * (np.pi * {1} * {2}) / (4.0 * np.log(10.0 ** ({3} / 20.0)))\n{0}_t = (np.arange({4}) - {5}) / {6}\n"
                       "{0} = (np.exp(-{0}_a * {0}_t * {0}_t) * np.cos(2 * np.pi * {1} * {0}_t) * {7})[np.newaxis, :]",
                       io.out[0], real(em, io, "fc"), real(em, io, "bw"), real(em, io, "bwr"), n, n == 0 ? std::string{"0.0"} : em.number(static_cast<double>(n - 1) / 2), fs,
                       real(em, io, "amplitude"));
}

core::Result<std::string> impulse_train(const NodeIo& io, Emitter& em) {
    const auto fs = rate_param(io);
    const auto period = pipeline::param_samples(io.node.params, "period", fs), offset = pipeline::param_samples(io.node.params, "offset", fs);
    if (!period || !offset || *period == 0) return std::unexpected{unsupported(io, "period")};
    return fmt::format("{0} = np.zeros((1, {1}))\n{0}[0, {2}::{3}] = 1.0\n{0} = {0} * {4}", io.out[0], source_length(io), *offset, *period, real(em, io, "amplitude"));
}

core::Result<std::string> shift(const NodeIo& io, Emitter&) {
    const auto s = param_int(io.node.params, "samples");
    if (param_text(io.node.params, "mode") == "circular") return fmt::format("{} = np.roll({}, {}, axis=1)", io.out[0], io.in[0], s);
    return fmt::format("{0} = np.zeros_like({1})\n{0}_n = {1}.shape[1]\n{0}[:, max(0, {2}):{0}_n + min(0, {2})] = {1}[:, max(0, -({2})):{0}_n - max(0, {2})]", io.out[0], io.in[0], s);
}

core::Result<std::string> biquad(const NodeIo& io, Emitter& em) {
    em.function("biquad");
    return fmt::format("{} = biquad({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], py_str(param_text(io.node.params, "type")), real(em, io, "f0"), real(em, io, "q"),
                       real(em, io, "gain_db"), io.in_fs[0]);
}

core::Result<std::string> compressor(const NodeIo& io, Emitter& em) {
    em.function("dynamics");
    return fmt::format("{} = dynamics({}, {}, \"compressor\", {}, ratio={}, knee={}, attack_ms={}, release_ms={}, makeup={}, detector={}, rms_ms={})", io.out[0], io.in[0], io.in_fs[0],
                       real(em, io, "threshold_db"), real(em, io, "ratio"), real(em, io, "knee_db"), real(em, io, "attack_ms"), real(em, io, "release_ms"), real(em, io, "makeup_db"),
                       py_str(param_text(io.node.params, "detector")), real(em, io, "rms_ms"));
}

core::Result<std::string> limiter(const NodeIo& io, Emitter& em) {
    em.function("dynamics");
    return fmt::format("{} = dynamics({}, {}, \"limiter\", {}, attack_ms={}, release_ms={})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "threshold_db"),
                       real(em, io, "attack_ms"), real(em, io, "release_ms"));
}

core::Result<std::string> gate(const NodeIo& io, Emitter& em) {
    em.function("dynamics");
    return fmt::format("{} = dynamics({}, {}, \"gate\", {}, attack_ms={}, release_ms={}, range_db={})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "threshold_db"),
                       real(em, io, "attack_ms"), real(em, io, "release_ms"), real(em, io, "range_db"));
}

double input_rate(const NodeIo& io) {
    if (!io.in_values.empty() && io.in_values[0])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[0])) return a->sample_rate;
    return 44100;
}

core::Result<std::string> echo(const NodeIo& io, Emitter& em) {
    const auto delay = static_cast<std::size_t>(std::floor(param_real(io.node.params, "delay_ms") / 1000 * input_rate(io) + 0.5));
    em.function("delay_lines");
    return fmt::format("{} = delay_lines({}, \"echo\", {}, {}, {})", io.out[0], io.in[0], delay, real(em, io, "feedback"), real(em, io, "mix"));
}

core::Result<std::string> comb(const NodeIo& io, Emitter& em) {
    const auto delay = pipeline::param_samples(io.node.params, "delay", input_rate(io));
    if (!delay) return std::unexpected{unsupported(io, "delay")};
    em.function("delay_lines");
    return fmt::format("{} = delay_lines({}, {}, {}, {})", io.out[0], io.in[0], py_str(param_text(io.node.params, "kind")), *delay, real(em, io, "gain"));
}

core::Result<std::string> mix(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const double fs = input_rate(io);
    em.function("mix_signals");
    return fmt::format("{} = mix_signals({}, {}, 10.0 ** ({} / 20), 10.0 ** ({} / 20), {}, {}, {})", io.out[0], io.in[0], io.in[1], real(em, io, "gain_a_db"), real(em, io, "gain_b_db"),
                       param_text(p, "mode") == "crossfade" ? "True" : "False", em.number(std::floor(param_real(p, "at") * fs + 0.5)),
                       em.number(std::floor(param_real(p, "crossfade_ms") / 1000 * fs + 0.5)));
}

}  // namespace

void register_effect_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"tremolo", tremolo}, {"modulated_delay", modulated_delay}, {"phaser", phaser}, {"reverb", reverb}, {"wavefold", wavefold}, {"am", am}, {"fm", fm},
             {"gausspulse", gausspulse}, {"impulse_train", impulse_train}, {"shift", shift}, {"biquad", biquad}, {"compressor", compressor},
             {"limiter", limiter}, {"gate", gate}, {"echo", echo}, {"comb", comb}, {"mix", mix}})
        out.push_back({id, 1e-12, fn});
}

}  // namespace signals::python
