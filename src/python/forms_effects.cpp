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

std::string real(Emitter& em, const NodeIo& io, std::string_view name) { return em.expr(io, name); }


core::Result<std::string> tremolo(const NodeIo& io, Emitter& em) {
    em.function("lfo");
    return fmt::format("{0} = {1} * (1 - {2} * (1 - lfo({3}, 2 * np.pi * {4} * np.arange({1}.shape[1]) / {5})) / 2)", io.out[0], io.in[0], real(em, io, "depth"),
                       em.expr(io, "shape"), real(em, io, "rate_hz"), io.in_fs[0]);
}

core::Result<std::string> modulated_delay(const NodeIo& io, Emitter& em) {
    const auto& mode = pipeline::param_text(io.node.params, "mode");
    std::string feedback, wet;
    if (uses_variable(io, "mode")) {
        const auto m = em.expr(io, "mode");
        feedback = fmt::format("({} if {} == \"flanger\" else 0.0)", real(em, io, "feedback"), m);
        wet = fmt::format("(1.0 if {} == \"vibrato\" else {})", m, real(em, io, "mix"));
    } else {
        feedback = mode == "flanger" ? real(em, io, "feedback") : std::string{"0.0"};
        wet = mode == "vibrato" ? std::string{"1.0"} : real(em, io, "mix");
    }
    em.function("modulated_delay");
    return fmt::format("{} = modulated_delay({}, {}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "rate_hz"), real(em, io, "depth_ms"), real(em, io, "base_ms"),
                       feedback, wet);
}

core::Result<std::string> phaser(const NodeIo& io, Emitter& em) {
    em.function("phaser");
    return fmt::format("{} = phaser({}, {}, {}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], em.expr(io, "stages"), real(em, io, "rate_hz"), real(em, io, "min_hz"),
                       real(em, io, "max_hz"), real(em, io, "feedback"), real(em, io, "mix"));
}

core::Result<std::string> reverb(const NodeIo& io, Emitter& em) {
    em.function("reverb");
    return fmt::format("{} = reverb({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], io.in_fs[0], real(em, io, "room_size"), real(em, io, "damping"), real(em, io, "wet"),
                       real(em, io, "dry"));
}

core::Result<std::string> wavefold(const NodeIo& io, Emitter& em) {
    const auto u = fmt::format("{} * {}", real(em, io, "drive"), io.in[0]);
    const auto sine = fmt::format("np.sin(np.pi / 2 * ({}))", u);
    const auto fold = fmt::format("{0}_v = np.fmod({1} + 1, 4.0)\n{0}_v = np.where({0}_v < 0, {0}_v + 4, {0}_v)\n{0} = np.where({0}_v < 2, {0}_v - 1, 3 - {0}_v)", io.out[0], u);
    if (uses_variable(io, "shape")) return fmt::format("{0}\nif {1} == \"sine\":\n    {2} = {3}", fold, em.expr(io, "shape"), io.out[0], sine);
    if (pipeline::param_text(io.node.params, "shape") == "sine") return fmt::format("{} = {}", io.out[0], sine);
    return fold;
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
    em.function("samples_count");
    const auto fs = em.expr(io, "rate");
    return fmt::format("{0}_n = samples_count({1}, {2})\n"
                       "{0}_a = -(np.pi * {3} * {4}) * (np.pi * {3} * {4}) / (4.0 * np.log(10.0 ** ({5} / 20.0)))\n{0}_t = (np.arange({0}_n) - ({0}_n - 1) / 2) / {2}\n"
                       "{0} = (np.exp(-{0}_a * {0}_t * {0}_t) * np.cos(2 * np.pi * {3} * {0}_t) * {6})[np.newaxis, :]",
                       io.out[0], em.expr(io, "duration"), fs, em.expr(io, "fc"), em.expr(io, "bw"), em.expr(io, "bwr"), em.expr(io, "amplitude"));
}

core::Result<std::string> impulse_train(const NodeIo& io, Emitter& em) {
    em.function("as_samples");
    const auto fs = em.expr(io, "rate");
    return fmt::format("{0} = np.zeros((1, samples_count({1}, {2})))\n{0}[0, as_samples({3}, {2})::as_samples({4}, {2})] = 1.0\n{0} = {0} * {5}", io.out[0],
                       em.expr(io, "duration"), fs, em.expr(io, "begin"), em.expr(io, "period"), em.expr(io, "amplitude"));
}

core::Result<std::string> shift(const NodeIo& io, Emitter& em) {
    const auto s = em.expr(io, "samples");
    const auto circular = fmt::format("np.roll({}, {}, axis=1)", io.in[0], s);
    const auto linear = fmt::format("{0}_v = np.zeros_like({1})\n{0}_n = {1}.shape[1]\n{0}_v[:, max(0, {2}):{0}_n + min(0, {2})] = {1}[:, max(0, -({2})):{0}_n - max(0, {2})]", io.out[0],
                                    io.in[0], s);
    if (uses_variable(io, "mode")) return fmt::format("{0}\n{1} = {2} if {3} == \"circular\" else {1}_v", linear, io.out[0], circular, em.expr(io, "mode"));
    if (pipeline::param_text(io.node.params, "mode") == "circular") return fmt::format("{} = {}", io.out[0], circular);
    return fmt::format("{0}\n{1} = {1}_v", linear, io.out[0]);
}

core::Result<std::string> biquad(const NodeIo& io, Emitter& em) {
    em.function("biquad");
    return fmt::format("{} = biquad({}, {}, {}, {}, {}, {})", io.out[0], io.in[0], em.expr(io, "type"), real(em, io, "f0"), real(em, io, "q"),
                       real(em, io, "gain_db"), io.in_fs[0]);
}

core::Result<std::string> compressor(const NodeIo& io, Emitter& em) {
    em.function("dynamics");
    return fmt::format("{} = dynamics({}, {}, \"compressor\", {}, ratio={}, knee={}, attack_ms={}, release_ms={}, makeup={}, detector={}, rms_ms={})", io.out[0], io.in[0], io.in_fs[0],
                       real(em, io, "threshold_db"), real(em, io, "ratio"), real(em, io, "knee_db"), real(em, io, "attack_ms"), real(em, io, "release_ms"), real(em, io, "makeup_db"),
                       em.expr(io, "detector"), real(em, io, "rms_ms"));
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

core::Result<std::string> echo(const NodeIo& io, Emitter& em) {
    em.function("delay_lines");
    return fmt::format("{} = delay_lines({}, \"echo\", int(np.floor({} / 1000 * {} + 0.5)), {}, {})", io.out[0], io.in[0], real(em, io, "delay_ms"), io.in_fs[0], real(em, io, "feedback"),
                       real(em, io, "mix"));
}

core::Result<std::string> comb(const NodeIo& io, Emitter& em) {
    em.function("delay_lines");
    return fmt::format("{} = delay_lines({}, {}, {}, {})", io.out[0], io.in[0], em.expr(io, "kind"), samples_expr(io, em, "delay", io.in_fs[0]), real(em, io, "gain"));
}

core::Result<std::string> mix(const NodeIo& io, Emitter& em) {
    em.function("mix_signals");
    const auto& fs = io.in_fs[0];
    return fmt::format("{} = mix_signals({}, {}, 10.0 ** ({} / 20), 10.0 ** ({} / 20), {}, np.floor({} * {} + 0.5), np.floor({} / 1000 * {} + 0.5))", io.out[0], io.in[0], io.in[1],
                       real(em, io, "gain_a_db"), real(em, io, "gain_b_db"), flag(io, em, "mode", "crossfade"), real(em, io, "at"), fs, real(em, io, "crossfade_ms"), fs);
}

core::Result<std::string> time_stretch(const NodeIo& io, Emitter& em) {
    em.function("time_stretch");
    return fmt::format("{} = time_stretch({}, {}, {}, {}, {})", io.out[0], io.in[0], em.expr(io, "factor"), em.expr(io, "method"), samples_expr(io, em, "frame", io.in_fs[0]),
                       samples_expr(io, em, "hop", io.in_fs[0]));
}

core::Result<std::string> pitch_shift(const NodeIo& io, Emitter& em) {
    em.function("pitch_shift");
    return fmt::format("{} = pitch_shift({}, {}, {}, {}, {})", io.out[0], io.in[0], em.expr(io, "semitones"), em.expr(io, "method"), samples_expr(io, em, "frame", io.in_fs[0]),
                       samples_expr(io, em, "hop", io.in_fs[0]));
}

core::Result<std::string> speed(const NodeIo& io, Emitter& em) {
    em.function("speed");
    return fmt::format("{} = speed({}, {})", io.out[0], io.in[0], em.expr(io, "factor"));
}

}  // namespace

void register_effect_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"tremolo", tremolo}, {"modulated_delay", modulated_delay}, {"phaser", phaser}, {"reverb", reverb}, {"wavefold", wavefold}, {"am", am}, {"fm", fm},
             {"gausspulse", gausspulse}, {"impulse_train", impulse_train}, {"shift", shift}, {"biquad", biquad}, {"compressor", compressor},
             {"limiter", limiter}, {"gate", gate}, {"echo", echo}, {"comb", comb}, {"mix", mix},
             {"speed", speed}})
        out.push_back({id, 1e-12, fn});
    out.push_back({"time_stretch", 1e-9, time_stretch});
    out.push_back({"pitch_shift", 1e-9, pitch_shift});
}

}  // namespace signals::python
