#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <fstream>

#include "core/signal_ops.hpp"
#include "pipeline/blocks_common.hpp"
#include "io/audio_file.hpp"
#include "io/generators.hpp"
#include "io/npy.hpp"
#include "io/npy_audio.hpp"
#include "python/form.hpp"

namespace signals::python {
namespace {

using pipeline::param_int;
using pipeline::param_real;
using pipeline::param_text;

core::Result<std::string> gen(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const double rate = static_cast<double>(param_int(p, "rate"));
    const auto n = static_cast<std::size_t>(std::llround(param_real(p, "duration") * rate));
    const auto& kind = param_text(p, "kind");
    const double f = param_real(p, "freq"), a = param_real(p, "amplitude");
    const auto channels = static_cast<std::size_t>(param_int(p, "channels"));
    const auto& y = io.out[0];
    const auto fs = em.number(rate), num = std::to_string(n);
    if (kind == "white" || kind == "pink") {
        em.function("noise_from_draws");
        std::vector<std::vector<double>> draws;
        if (!em.preview())
            for (std::size_t c = 0; c < channels; ++c) draws.push_back(io::normal_draws(n, static_cast<std::uint64_t>(param_int(p, "seed")) + c));
        const auto data = em.data("draws", io::np_matrix(draws));
        return fmt::format("{} = np.stack([noise_from_draws(d, {}, {}) for d in {}])", y, py_str(kind), em.number(a), data);
    }
    std::string x;
    if (kind == "sine") x = fmt::format("{} * np.sin(2 * np.pi * {} * np.arange({}) / {})", em.number(a), em.number(f), num, fs);
    else if (kind == "sweep") em.function("sweep"), x = fmt::format("sweep({}, {}, True, {}, {})", em.number(f), em.number(param_real(p, "f1")), fs, num);
    else if (kind == "impulse") x = fmt::format("(np.arange({}) == 0) * 1.0", num);
    else if (kind == "step") x = fmt::format("np.ones({})", num);
    else if (kind == "harmonics") {
        std::string sum;
        for (int h = 0; h < 4; ++h)
            sum += (h ? " + " : "") + fmt::format("{} * np.sin(2 * np.pi * {} * np.arange({}) / {})", em.number(a / std::ldexp(1.0, h)), em.number((h + 1) * f), num, fs);
        x = sum;
    } else {
        if (kind != "doppler" && kind != "blocks" && kind != "bumps" && kind != "heavisine") return std::unexpected{unsupported(io, "kind")};
        em.function("donoho_johnstone");
        x = fmt::format("donoho_johnstone({}, {}) * {}", py_str(kind), num, em.number(a));
    }
    return fmt::format("{} = np.tile({}, ({}, 1))", y, x, channels);
}

double in_rate(const NodeIo& io, std::size_t k = 0) {
    if (k < io.in_values.size() && io.in_values[k])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[k])) return a->sample_rate;
    return 44100;
}

std::size_t samples_param(const NodeIo& io, std::string_view name) {
    const auto n = pipeline::param_samples(io.node.params, name, in_rate(io));
    return n ? *n : 0;
}

std::string rows_literal(const NodeIo& io, Emitter& em, const std::string& key, const std::vector<std::vector<double>>& rows) {
    std::string out = "np.array([";
    for (std::size_t c = 0; c < rows.size(); ++c) out += (c ? ", " : "") + em.numbers(key + std::to_string(c), rows[c]);
    (void)io;
    return out + "], dtype=np.float64)";
}

bool scipy_reads(const std::filesystem::path& path, const io::WavInfo& info) {
    std::ifstream in{path, std::ios::binary};
    char tag[4] = {};
    in.read(tag, 4);
    if (std::string_view{tag, 4} != "RIFF") return false;
    using F = io::SampleFormat;
    return info.container == io::Container::wav &&
           (info.format == F::pcm_u8 || info.format == F::pcm_s16 || info.format == F::pcm_s24 || info.format == F::pcm_s32 || info.format == F::float32 || info.format == F::float64);
}

core::Result<std::string> wav(const NodeIo& io, Emitter& em) {
    auto path = pipeline::utf8_path(param_text(io.node.params, "path"));
    if (path.is_relative()) path = io.base_dir / path;
    const auto& y = io.out[0];
    const auto& key = param_text(io.node.params, "key");
    if (!em.preview() && io::is_array_audio(path)) {
        em.function("read_audio_array");
        std::ifstream in{path, std::ios::binary};
        char b = 0;
        in.get(b);
        const bool npy = static_cast<unsigned char>(b) == 0x93;
        if (npy) return fmt::format("{} = read_audio_array({}, None)", y, em.file(path));
        const auto picked = io::array_audio_key(path, key);
        if (!picked) return std::unexpected{picked.error()};
        return fmt::format("{} = read_audio_array({}, {})", y, em.file(path), py_str(*picked));
    }
    const auto info = em.preview() ? core::Result<io::WavInfo>{io::WavInfo{}} : io::probe_audio(path);
    if (em.preview() || (info && scipy_reads(path, *info))) {
        em.function("read_wav");
        return fmt::format("{}, {}_fs = read_wav({})", y, y, em.file(path));
    }
    std::vector<std::vector<double>> rows;
    if (io.out_values[0])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.out_values[0])) rows = a->channels;
    return fmt::format("{} = {}", y, em.data("x", io::np_matrix(rows)));
}

core::Result<std::string> samples(const NodeIo& io, Emitter& em) {
    std::vector<std::vector<double>> rows;
    if (io.out_values[0])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.out_values[0])) rows = a->channels;
    return fmt::format("{} = {}", io.out[0], rows_literal(io, em, "values", rows));
}

core::Result<std::string> add(const NodeIo& io, Emitter& em) {
    em.function("add_signals");
    return fmt::format("{} = add_signals({}, {}, {})", io.out[0], io.in[0], io.in[1], param_text(io.node.params, "op") == "sub" ? "-1.0" : "1.0");
}

core::Result<std::string> normalize(const NodeIo& io, Emitter& em) {
    em.function("normalize");
    return fmt::format("{} = normalize({}, {}, {})", io.out[0], io.in[0], py_str(param_text(io.node.params, "kind")), em.number(param_real(io.node.params, "target_dbfs")));
}

core::Result<std::string> remove_dc(const NodeIo& io, Emitter& em) {
    em.function("remove_dc");
    return fmt::format("{} = remove_dc({})", io.out[0], io.in[0]);
}

core::Result<std::string> clip(const NodeIo& io, Emitter& em) {
    const auto limit = em.number(param_real(io.node.params, "limit"));
    return fmt::format("{} = np.clip({}, -{}, {})", io.out[0], io.in[0], limit, limit);
}

core::Result<std::string> quantize(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto bits = param_int(p, "bits");
    std::string dither;
    if (param_text(p, "dither") == "yes") {
        std::vector<std::vector<double>> rows;
        if (!em.preview() && io.in_values[0])
            if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[0]))
                for (std::size_t c = 0; c < a->channels.size(); ++c) rows.push_back(core::tpdf_draws(a->channels[c].size(), static_cast<std::uint64_t>(param_int(p, "seed")) + c));
        dither = " + " + em.data("dither", io::np_matrix(rows));
    }
    const auto scale = fmt::format("2.0 ** {}", bits - 1);
    return fmt::format("{0}_scale = {1}\n{0} = np.clip(np.rint(np.where(np.isfinite({2}), {2}, 0.0) * {0}_scale{3}), -{0}_scale, {0}_scale - 1) / {0}_scale", io.out[0], scale,
                       io.in[0], dither);
}

core::Result<std::string> mulaw(const NodeIo& io, Emitter& em) {
    const auto mu = em.number(param_real(io.node.params, "mu"));
    const auto v = fmt::format("np.clip(np.nan_to_num({}, nan=0.0), -1.0, 1.0)", io.in[0]);
    if (param_text(io.node.params, "direction") == "encode")
        return fmt::format("{0}_v = {1}\n{0} = np.copysign(np.log1p({2} * np.abs({0}_v)) / np.log1p({2}), {0}_v)", io.out[0], v, mu);
    return fmt::format("{0}_v = {1}\n{0} = np.copysign(np.expm1(np.abs({0}_v) * np.log1p({2})) / {2}, {0}_v)", io.out[0], v, mu);
}

core::Result<std::string> split(const NodeIo& io, Emitter&) {
    const auto& x = io.in[0];
    if (param_text(io.node.params, "mode") == "channel") {
        const auto c = param_int(io.node.params, "channel");
        return fmt::format("{0} = {2}[{3}:{4}]\n{1} = {0}", io.out[0], io.out[1], x, c, c + 1);
    }
    return fmt::format("{0} = {2}[0:1]\n{1} = {2}[1:2] if {2}.shape[0] > 1 else {0}", io.out[0], io.out[1], x);
}

core::Result<std::string> mixdown(const NodeIo& io, Emitter& em) {
    em.function("mixdown");
    return fmt::format("{} = mixdown({})", io.out[0], io.in[0]);
}

core::Result<std::string> mid_side(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = np.stack([({1}[0] + {1}[1]) * (np.sqrt(2.0) / 2), ({1}[0] - {1}[1]) * (np.sqrt(2.0) / 2)])", io.out[0], io.in[0]);
}

core::Result<std::string> delay(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = np.concatenate([np.zeros(({1}.shape[0], {2})), {1}], axis=1)", io.out[0], io.in[0], samples_param(io, "samples"));
}

core::Result<std::string> reverse(const NodeIo& io, Emitter&) { return fmt::format("{} = {}[:, ::-1]", io.out[0], io.in[0]); }

core::Result<std::string> cut(const NodeIo& io, Emitter&) {
    const auto b = samples_param(io, "begin"), e = samples_param(io, "end");
    return fmt::format("{} = {}[:, {}:{}]", io.out[0], io.in[0], b, e == 0 ? std::string{} : std::to_string(e));
}

core::Result<std::string> merge(const NodeIo& io, Emitter& em) {
    em.function("merge_signals");
    return fmt::format("{} = merge_signals({}, {})", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> swap(const NodeIo& io, Emitter&) { return fmt::format("{0} = {1}[np.r_[1, 0, 2:{1}.shape[0]]]", io.out[0], io.in[0]); }

core::Result<std::string> pan(const NodeIo& io, Emitter& em) {
    em.function("mixdown");
    const auto pos = em.number(param_real(io.node.params, "position"));
    if (param_text(io.node.params, "law") == "linear")
        return fmt::format("{0}_mono = mixdown({1})[0]\n{0} = np.stack([(1 - {2}) / 2 * {0}_mono, (1 + {2}) / 2 * {0}_mono])", io.out[0], io.in[0], pos);
    return fmt::format("{0}_mono = mixdown({1})[0]\n{0}_angle = ({2} + 1) * np.pi / 4\n{0} = np.stack([np.cos({0}_angle) * {0}_mono, np.sin({0}_angle) * {0}_mono])", io.out[0],
                       io.in[0], pos);
}

core::Result<std::string> duplicate(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = {1}[np.arange({2}) % {1}.shape[0]]", io.out[0], io.in[0], param_int(io.node.params, "channels"));
}

core::Result<std::string> pad(const NodeIo& io, Emitter&) {
    return fmt::format("{} = np.pad({}, ((0, 0), ({}, {})))", io.out[0], io.in[0], samples_param(io, "before"), samples_param(io, "after"));
}

core::Result<std::string> concat(const NodeIo& io, Emitter& em) {
    em.function("pairwise");
    return fmt::format("{} = pairwise({}, {}, lambda a, b: np.concatenate([a, b]))", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> multiply(const NodeIo& io, Emitter& em) {
    const auto factor = em.number(param_real(io.node.params, "factor"));
    if (io.in.size() < 2 || io.in[1].empty()) return fmt::format("{} = {} * {}", io.out[0], factor, io.in[0]);
    em.function("pairwise");
    em.function("padded");
    return fmt::format("{0} = pairwise({1}, {2}, lambda a, b: padded({3} * a[: min(a.size, b.size)] * b[: min(a.size, b.size)], max(a.size, b.size)))", io.out[0], io.in[0], io.in[1],
                       factor);
}

core::Result<std::string> window(const NodeIo& io, Emitter& em) {
    em.function("window_of");
    const auto& p = io.node.params;
    return fmt::format("{0} = {1} * window_of({2}, {1}.shape[1], {3}, {4})", io.out[0], io.in[0], py_str(param_text(p, "kind")), em.number(param_real(p, "beta")),
                       em.number(param_real(p, "alpha")));
}

core::Result<std::string> fade(const NodeIo& io, Emitter& em) {
    em.function("fade");
    return fmt::format("{} = fade({}, {}, {}, {})", io.out[0], io.in[0], samples_param(io, "fade_in"), samples_param(io, "fade_out"),
                       param_text(io.node.params, "curve") == "cosine" ? "True" : "False");
}

core::Result<std::string> elementwise(const NodeIo& io, std::string_view expr) { return fmt::format("{} = {}", io.out[0], fmt::format(fmt::runtime(expr), io.in[0])); }

core::Result<std::string> invert(const NodeIo& io, Emitter&) { return elementwise(io, "-{}"); }
core::Result<std::string> absolute(const NodeIo& io, Emitter&) { return elementwise(io, "np.abs({})"); }
core::Result<std::string> square(const NodeIo& io, Emitter&) { return elementwise(io, "{0} * {0}"); }
core::Result<std::string> diff(const NodeIo& io, Emitter&) { return elementwise(io, "np.diff({}, axis=1, prepend=0.0)"); }
core::Result<std::string> cumsum(const NodeIo& io, Emitter&) { return elementwise(io, "np.cumsum({}, axis=1)"); }

core::Result<std::string> softclip(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = np.tanh({} * {})", io.out[0], em.number(param_real(io.node.params, "drive")), io.in[0]);
}

core::Result<std::string> dc_offset(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = {} + {}", io.out[0], io.in[0], em.number(param_real(io.node.params, "value")));
}

core::Result<std::string> trim_silence(const NodeIo& io, Emitter& em) {
    em.function("trim_silence");
    return fmt::format("{} = trim_silence({}, {})", io.out[0], io.in[0], em.number(param_real(io.node.params, "threshold_db")));
}

core::Result<std::string> affine(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    return fmt::format("{} = {} * {} + {}", io.out[0], em.number(param_real(p, "scale")), io.in[0], em.number(param_real(p, "offset")));
}

core::Result<std::string> math(const NodeIo& io, Emitter& em) {
    const auto& op = param_text(io.node.params, "op");
    const auto& x = io.in[0];
    std::string e;
    if (op == "log") e = "np.log(" + x + ")";
    else if (op == "log10") e = "np.log10(" + x + ")";
    else if (op == "exp") e = "np.exp(" + x + ")";
    else if (op == "sqrt") e = "np.sqrt(" + x + ")";
    else if (op == "pow") e = "np.power(" + x + ", " + em.number(param_real(io.node.params, "p")) + ")";
    else if (op == "sign") e = "np.sign(" + x + ")";
    else if (op == "reciprocal") e = "1 / " + x;
    else return std::unexpected{unsupported(io, "op")};
    return fmt::format("{} = {}", io.out[0], e);
}

core::Result<std::string> binary(const NodeIo& io, Emitter& em) {
    const auto& op = param_text(io.node.params, "op");
    std::string e;
    if (op == "divide") e = "a / b";
    else if (op == "min") e = "np.minimum(a, b)";
    else if (op == "max") e = "np.maximum(a, b)";
    else if (op == "pow") e = "np.power(a, b)";
    else if (op == "hypot") e = "np.hypot(a, b)";
    else return std::unexpected{unsupported(io, "op")};
    em.function("pairwise");
    em.function("padded");
    return fmt::format("{0} = pairwise({1}, {2}, lambda u, v: (lambda a, b: {3})(padded(u, max(u.size, v.size)), padded(v, max(u.size, v.size))))", io.out[0], io.in[0], io.in[1], e);
}

core::Result<std::string> repeat(const NodeIo& io, Emitter&) {
    return fmt::format("{} = np.tile({}, (1, {}))", io.out[0], io.in[0], param_int(io.node.params, "times"));
}

core::Result<std::string> select_channel(const NodeIo& io, Emitter&) {
    const auto i = param_int(io.node.params, "index");
    return fmt::format("{} = {}[{}:{}]", io.out[0], io.in[0], i, i + 1);
}

core::Result<std::string> channel_delay(const NodeIo& io, Emitter& em) {
    const auto delays = pipeline::blocks::parse_numbers("channel_delay", "delays", param_text(io.node.params, "delays"));
    if (!delays || delays->empty()) return std::unexpected{unsupported(io, "delays")};
    std::string list;
    for (const double d : *delays) list += (list.empty() ? "" : ", ") + std::to_string(static_cast<long long>(d));
    em.function("shifted");
    return fmt::format("{0}_delays = [{2}]\n{0} = np.stack([shifted(row, {0}_delays[min(c, len({0}_delays) - 1)]) for c, row in enumerate({1})])", io.out[0], io.in[0], list);
}

core::Result<std::string> stereo_width(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = {1}.copy()\n{0}_mid = ({1}[0] + {1}[1]) / 2\n{0}_side = ({1}[0] - {1}[1]) / 2\n{0}[0] = {0}_mid + {2} * {0}_side\n{0}[1] = {0}_mid - {2} * {0}_side",
                       io.out[0], io.in[0], em.number(param_real(io.node.params, "width")));
}

core::Result<std::string> balance(const NodeIo& io, Emitter& em) {
    const auto pos = em.number(param_real(io.node.params, "position"));
    return fmt::format("{0} = {1}.copy()\n{0}[0] = np.sqrt(1 - {2}) * {1}[0]\n{0}[1] = np.sqrt(1 + {2}) * {1}[1]", io.out[0], io.in[0], pos);
}

core::Result<std::string> sample_hold(const NodeIo& io, Emitter&) {
    const auto h = samples_param(io, "hold");
    return fmt::format("{0} = {1}[:, np.arange({1}.shape[1]) - np.arange({1}.shape[1]) % {2}]", io.out[0], io.in[0], h);
}

core::Result<std::string> dc_block(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    return fmt::format("{} = signal.lfilter([1.0, -1.0], [1.0, -{}], {}, axis=1)", io.out[0], em.number(param_real(io.node.params, "r")), io.in[0]);
}

core::Result<std::string> add_noise(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    std::vector<std::vector<double>> draws;
    if (!em.preview() && io.in_values[0])
        if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[0]))
            for (std::size_t c = 0; c < a->channels.size(); ++c) draws.push_back(io::normal_draws(a->channels[c].size(), static_cast<std::uint64_t>(param_int(p, "seed")) + c));
    em.function("noise_from_draws");
    const auto& x = io.in[0];
    return fmt::format("{0}_rms = np.sqrt(np.sum({1} * {1}) / {1}.size / 10 ** ({2} / 10))\n{0} = np.stack([noise_from_draws(d, {3}, {0}_rms) for d in {4}]) + {1}", io.out[0], x,
                       em.number(param_real(p, "snr_db")), py_str(param_text(p, "color")), em.data("draws", io::np_matrix(draws)));
}

std::size_t source_length(const pipeline::Params& p) {
    return static_cast<std::size_t>(std::llround(param_real(p, "duration") * static_cast<double>(param_int(p, "rate"))));
}

core::Result<std::string> chirp(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    em.function("sweep");
    return fmt::format("{} = (sweep({}, {}, {}, {}, {}) * {})[np.newaxis, :]", io.out[0], em.number(param_real(p, "f0")), em.number(param_real(p, "f1")),
                       param_text(p, "method") == "log" ? "True" : "False", em.number(static_cast<double>(param_int(p, "rate"))), source_length(p), em.number(param_real(p, "amplitude")));
}

core::Result<std::string> osc(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    em.function("osc");
    return fmt::format("{} = (osc({}, {}, {}, {}, {}, {}) * {})[np.newaxis, :]", io.out[0], py_str(param_text(p, "kind")), em.number(param_real(p, "freq")),
                       em.number(param_real(p, "duty")), em.number(param_real(p, "width")), em.number(static_cast<double>(param_int(p, "rate"))), source_length(p),
                       em.number(param_real(p, "amplitude")));
}

core::Result<std::string> gain(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = {} * 10 ** ({} / 20)", io.out[0], io.in[0], em.number(param_real(io.node.params, "db")));
}

core::Result<std::string> wav_out(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& bits = param_text(p, "bits");
    const bool dither = param_text(p, "dither") == "tpdf" && bits != "f32";
    std::string dither_expr = "None";
    if (dither) {
        std::size_t count = 0;
        if (io.in_values[0])
            if (const auto* a = std::get_if<core::AudioBuffer>(io.in_values[0])) count = a->frames() * a->channels.size();
        dither_expr = em.data("dither", io::np_vector(em.preview() ? std::vector<double>{} : core::tpdf_draws(count, static_cast<std::uint64_t>(param_int(p, "seed")))));
    }
    em.function("write_wav");
    auto path = pipeline::utf8_path(param_text(p, "path"));
    if (path.is_relative()) path = io.base_dir / path;
    const auto& y = io.out[0];
    return fmt::format("{0}_path = {1}\n{0} = {{\"path\": str({0}_path), \"clipped\": float(write_wav({0}_path, {2}, {3}, {4}, {5}))}}\n{0}_ok = True", y, em.file(path), io.in[0],
                       io.in_fs[0], py_str(bits), dither_expr);
}

}  // namespace

void register_basic_forms(std::vector<PyForm>& out) {
    for (auto [id, fn] : std::initializer_list<std::pair<const char*, core::Result<std::string> (*)(const NodeIo&, Emitter&)>>{
             {"wav", wav},         {"samples", samples}, {"add", add},         {"normalize", normalize}, {"remove_dc", remove_dc}, {"clip", clip},
             {"quantize", quantize}, {"mulaw", mulaw},   {"split", split},     {"mixdown", mixdown},     {"mid_side", mid_side},   {"delay", delay},
             {"reverse", reverse}, {"cut", cut},         {"merge", merge},     {"swap", swap},           {"pan", pan},             {"duplicate", duplicate},
             {"pad", pad}, {"concat", concat}, {"multiply", multiply}, {"window", window}, {"fade", fade}, {"invert", invert}, {"abs", absolute}, {"square", square},
             {"diff", diff}, {"cumsum", cumsum}, {"softclip", softclip}, {"dc_offset", dc_offset}, {"trim_silence", trim_silence}, {"affine", affine}, {"math", math},
             {"binary", binary}, {"repeat", repeat}, {"select_channel", select_channel}, {"channel_delay", channel_delay}, {"stereo_width", stereo_width},
             {"balance", balance}, {"sample_hold", sample_hold}, {"dc_block", dc_block}, {"add_noise", add_noise}, {"chirp", chirp}, {"osc", osc}})
        out.push_back({id, 1e-12, fn});
    out.push_back({"gen", 1e-12, gen});
    out.push_back({"gain", 1e-12, gain});
    out.push_back({"wav_out", 1e-12, wav_out});
}

}  // namespace signals::python
