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
    const auto& y = io.out[0];
    const auto f = em.expr(io, "freq"), a = em.expr(io, "amplitude"), fs = em.expr(io, "rate"), channels = em.expr(io, "channels");
    if (uses_variable(io, "kind")) {
        em.function("gen_signal");
        return fmt::format("{} = gen_signal({}, freq={}, f1={}, amplitude={}, fs={}, duration={}, seed={}, channels={})", y, em.expr(io, "kind"), f, em.expr(io, "f1"), a, fs,
                           em.expr(io, "duration"), em.expr(io, "seed"), channels);
    }
    const auto& kind = param_text(io.node.params, "kind");
    em.function("samples_count");
    const auto n = y + "_n";
    const auto head = fmt::format("{} = samples_count({}, {})\n", n, em.expr(io, "duration"), fs);
    if (kind == "white" || kind == "pink") {
        em.function("portable_random");
        em.function("noise_from_draws");
        return head + fmt::format("{} = np.stack([noise_from_draws(portable_random({} + c, {}), {}, {}) for c in range({})])", y, em.expr(io, "seed"), n, py_str(kind), a, channels);
    }
    std::string x;
    if (kind == "sine") x = fmt::format("{} * np.sin(2 * np.pi * {} * np.arange({}) / {})", a, f, n, fs);
    else if (kind == "sweep") em.function("sweep"), x = fmt::format("sweep({}, {}, True, {}, {})", f, em.expr(io, "f1"), fs, n);
    else if (kind == "impulse") x = fmt::format("(np.arange({}) == 0) * 1.0", n);
    else if (kind == "step") x = fmt::format("np.ones({})", n);
    else if (kind == "harmonics") x = fmt::format("sum({} / 2 ** h * np.sin(2 * np.pi * ((h + 1) * {}) * np.arange({}) / {}) for h in range(4))", a, f, n, fs);
    else {
        if (kind != "doppler" && kind != "blocks" && kind != "bumps" && kind != "heavisine") return std::unexpected{unsupported(io, "kind")};
        em.function("donoho_johnstone");
        x = fmt::format("donoho_johnstone({}, {}) * {}", py_str(kind), n, a);
    }
    return head + fmt::format("{} = np.tile({}, ({}, 1))", y, x, channels);
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
        if (uses_variable(io, "key")) return fmt::format("{} = read_audio_array({}, {})", y, em.file(path), em.expr(io, "key"));
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
    em.function("samples_signal");
    return fmt::format("{} = samples_signal({})", io.out[0], em.expr(io, "values"));
}

core::Result<std::string> add(const NodeIo& io, Emitter& em) {
    em.function("add_signals");
    const auto sign = uses_variable(io, "op") ? "(-1.0 if " + em.expr(io, "op") + " == \"sub\" else 1.0)" : std::string{param_text(io.node.params, "op") == "sub" ? "-1.0" : "1.0"};
    return fmt::format("{} = add_signals({}, {}, {})", io.out[0], io.in[0], io.in[1], sign);
}

core::Result<std::string> normalize(const NodeIo& io, Emitter& em) {
    em.function("normalize");
    return fmt::format("{} = normalize({}, {}, {})", io.out[0], io.in[0], em.expr(io, "kind"), em.expr(io, "target_dbfs"));
}

core::Result<std::string> remove_dc(const NodeIo& io, Emitter& em) {
    em.function("remove_dc");
    return fmt::format("{} = remove_dc({})", io.out[0], io.in[0]);
}

core::Result<std::string> clip(const NodeIo& io, Emitter& em) {
    const auto limit = em.expr(io, "limit");
    return fmt::format("{} = np.clip({}, -{}, {})", io.out[0], io.in[0], limit, limit);
}

core::Result<std::string> quantize(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& x = io.in[0];
    std::string dither;
    const auto draws = fmt::format("np.stack([portable_random({} + c, {}.shape[1], \"tpdf\") for c in range({}.shape[0])])", em.expr(io, "seed"), x, x);
    if (uses_variable(io, "dither")) {
        em.function("portable_random");
        dither = fmt::format(" + ({} if {} == \"yes\" else 0.0)", draws, em.expr(io, "dither"));
    } else if (param_text(p, "dither") == "yes") {
        em.function("portable_random");
        dither = " + " + draws;
    }
    return fmt::format("{0}_scale = 2.0 ** ({1} - 1)\n{0} = np.clip(np.rint(np.where(np.isfinite({2}), {2}, 0.0) * {0}_scale{3}), -{0}_scale, {0}_scale - 1) / {0}_scale", io.out[0],
                       em.expr(io, "bits"), x, dither);
}

core::Result<std::string> mulaw(const NodeIo& io, Emitter& em) {
    const auto mu = em.expr(io, "mu");
    const auto v = fmt::format("np.clip(np.nan_to_num({}, nan=0.0), -1.0, 1.0)", io.in[0]);
    const auto encode = fmt::format("np.copysign(np.log1p({1} * np.abs({0}_v)) / np.log1p({1}), {0}_v)", io.out[0], mu);
    const auto decode = fmt::format("np.copysign(np.expm1(np.abs({0}_v) * np.log1p({1})) / {1}, {0}_v)", io.out[0], mu);
    if (uses_variable(io, "direction")) return fmt::format("{0}_v = {1}\n{0} = {2} if {3} == \"encode\" else {4}", io.out[0], v, encode, em.expr(io, "direction"), decode);
    return fmt::format("{0}_v = {1}\n{0} = {2}", io.out[0], v, param_text(io.node.params, "direction") == "encode" ? encode : decode);
}

core::Result<std::string> split(const NodeIo& io, Emitter& em) {
    const auto& x = io.in[0];
    const auto c = em.expr(io, "channel");
    const auto by_channel = fmt::format("{0} = {2}[{3}:{3} + 1]\n{1} = {0}", io.out[0], io.out[1], x, c);
    const auto stereo = fmt::format("{0} = {2}[0:1]\n{1} = {2}[1:2] if {2}.shape[0] > 1 else {0}", io.out[0], io.out[1], x);
    if (uses_variable(io, "mode")) {
        const auto indent = [](const std::string& code) {
            std::string out;
            for (std::size_t at = 0; at < code.size();) {
                const auto end = std::min(code.find('\n', at), code.size());
                out += "    " + code.substr(at, end - at) + "\n";
                at = end + 1;
            }
            return out;
        };
        return fmt::format("if {} == \"channel\":\n{}else:\n{}", em.expr(io, "mode"), indent(by_channel), indent(stereo));
    }
    return param_text(io.node.params, "mode") == "channel" ? by_channel : stereo;
}

core::Result<std::string> mixdown(const NodeIo& io, Emitter& em) {
    em.function("mixdown");
    return fmt::format("{} = mixdown({})", io.out[0], io.in[0]);
}

core::Result<std::string> mid_side(const NodeIo& io, Emitter&) {
    return fmt::format("{0} = np.stack([({1}[0] + {1}[1]) * (np.sqrt(2.0) / 2), ({1}[0] - {1}[1]) * (np.sqrt(2.0) / 2)])", io.out[0], io.in[0]);
}

core::Result<std::string> delay(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = np.concatenate([np.zeros(({1}.shape[0], {2})), {1}], axis=1)", io.out[0], io.in[0], samples_expr(io, em, "samples", io.in_fs[0]));
}

core::Result<std::string> reverse(const NodeIo& io, Emitter&) { return fmt::format("{} = {}[:, ::-1]", io.out[0], io.in[0]); }

core::Result<std::string> cut(const NodeIo& io, Emitter& em) {
    const auto b = samples_expr(io, em, "begin", io.in_fs[0]);
    std::string e;
    if (uses_variable(io, "end") || param_text(io.node.params, "end").ends_with('s')) e = "(" + samples_expr(io, em, "end", io.in_fs[0]) + " or None)";
    else if (const auto n = param_int(io.node.params, "end"); n != 0) e = std::to_string(n);
    return fmt::format("{} = {}[:, {}:{}]", io.out[0], io.in[0], b, e);
}

core::Result<std::string> merge(const NodeIo& io, Emitter& em) {
    em.function("merge_signals");
    return fmt::format("{} = merge_signals({}, {})", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> swap(const NodeIo& io, Emitter&) { return fmt::format("{0} = {1}[np.r_[1, 0, 2:{1}.shape[0]]]", io.out[0], io.in[0]); }

core::Result<std::string> pan(const NodeIo& io, Emitter& em) {
    em.function("mixdown");
    const auto pos = em.expr(io, "position");
    const auto linear = fmt::format("np.stack([(1 - {1}) / 2 * {0}_mono, (1 + {1}) / 2 * {0}_mono])", io.out[0], pos);
    const auto power = fmt::format("np.stack([np.cos({0}_angle) * {0}_mono, np.sin({0}_angle) * {0}_mono])", io.out[0]);
    if (uses_variable(io, "law"))
        return fmt::format("{0}_mono = mixdown({1})[0]\n{0}_angle = ({2} + 1) * np.pi / 4\n{0} = {3} if {4} == \"linear\" else {5}", io.out[0], io.in[0], pos, linear, em.expr(io, "law"),
                           power);
    if (param_text(io.node.params, "law") == "linear") return fmt::format("{0}_mono = mixdown({1})[0]\n{0} = {2}", io.out[0], io.in[0], linear);
    return fmt::format("{0}_mono = mixdown({1})[0]\n{0}_angle = ({2} + 1) * np.pi / 4\n{0} = {3}", io.out[0], io.in[0], pos, power);
}

core::Result<std::string> duplicate(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = {1}[np.arange({2}) % {1}.shape[0]]", io.out[0], io.in[0], em.expr(io, "channels"));
}

core::Result<std::string> pad(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = np.pad({}, ((0, 0), ({}, {})))", io.out[0], io.in[0], samples_expr(io, em, "before", io.in_fs[0]), samples_expr(io, em, "after", io.in_fs[0]));
}

core::Result<std::string> concat(const NodeIo& io, Emitter& em) {
    em.function("pairwise");
    return fmt::format("{} = pairwise({}, {}, lambda a, b: np.concatenate([a, b]))", io.out[0], io.in[0], io.in[1]);
}

core::Result<std::string> multiply(const NodeIo& io, Emitter& em) {
    const auto factor = em.expr(io, "scale");
    if (io.in.size() < 2 || io.in[1].empty()) return fmt::format("{} = {} * {}", io.out[0], factor, io.in[0]);
    em.function("pairwise");
    em.function("padded");
    return fmt::format("{0} = pairwise({1}, {2}, lambda a, b: padded({3} * a[: min(a.size, b.size)] * b[: min(a.size, b.size)], max(a.size, b.size)))", io.out[0], io.in[0], io.in[1],
                       factor);
}

core::Result<std::string> window(const NodeIo& io, Emitter& em) {
    em.function("window_of");
    return fmt::format("{0} = {1} * window_of({2}, {1}.shape[1], {3}, {4})", io.out[0], io.in[0], em.expr(io, "kind"), em.expr(io, "beta"),
                       em.expr(io, "alpha"));
}

core::Result<std::string> fade(const NodeIo& io, Emitter& em) {
    em.function("fade");
    return fmt::format("{} = fade({}, {}, {}, {})", io.out[0], io.in[0], samples_expr(io, em, "fade_in", io.in_fs[0]), samples_expr(io, em, "fade_out", io.in_fs[0]),
                       flag(io, em, "curve", "cosine"));
}

core::Result<std::string> elementwise(const NodeIo& io, std::string_view expr) { return fmt::format("{} = {}", io.out[0], fmt::format(fmt::runtime(expr), io.in[0])); }

core::Result<std::string> invert(const NodeIo& io, Emitter&) { return elementwise(io, "-{}"); }
core::Result<std::string> absolute(const NodeIo& io, Emitter&) { return elementwise(io, "np.abs({})"); }
core::Result<std::string> square(const NodeIo& io, Emitter&) { return elementwise(io, "{0} * {0}"); }
core::Result<std::string> diff(const NodeIo& io, Emitter&) { return elementwise(io, "np.diff({}, axis=1, prepend=0.0)"); }
core::Result<std::string> cumsum(const NodeIo& io, Emitter&) { return elementwise(io, "np.cumsum({}, axis=1)"); }

core::Result<std::string> softclip(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = np.tanh({} * {})", io.out[0], em.expr(io, "drive"), io.in[0]);
}

core::Result<std::string> dc_offset(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = {} + {}", io.out[0], io.in[0], em.expr(io, "offset"));
}

core::Result<std::string> trim_silence(const NodeIo& io, Emitter& em) {
    em.function("trim_silence");
    return fmt::format("{} = trim_silence({}, {})", io.out[0], io.in[0], em.expr(io, "threshold_db"));
}

core::Result<std::string> affine(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = {} * {} + {}", io.out[0], em.expr(io, "scale"), io.in[0], em.expr(io, "offset"));
}

core::Result<std::string> math(const NodeIo& io, Emitter& em) {
    const std::vector<std::pair<std::string, std::string>> ops{{"log", "np.log(@)"},   {"log10", "np.log10(@)"},          {"exp", "np.exp(@)"},  {"sqrt", "np.sqrt(@)"},
                                                               {"pow", "np.power(@, " + em.expr(io, "p") + ")"}, {"sign", "np.sign(@)"}, {"reciprocal", "1 / @"}};
    const auto with = [](std::string code, const std::string& x) {
        for (auto at = code.find('@'); at != std::string::npos; at = code.find('@', at + x.size())) code.replace(at, 1, x);
        return code;
    };
    if (uses_variable(io, "op")) {
        std::string table;
        for (const auto& [name, code] : ops) table += (table.empty() ? "" : ", ") + py_str(name) + ": lambda v: " + with(code, "v");
        return fmt::format("{} = {{{}}}[{}]({})", io.out[0], table, em.expr(io, "op"), io.in[0]);
    }
    const auto& op = param_text(io.node.params, "op");
    for (const auto& [name, code] : ops)
        if (name == op) return fmt::format("{} = {}", io.out[0], with(code, io.in[0]));
    return std::unexpected{unsupported(io, "op")};
}

core::Result<std::string> binary(const NodeIo& io, Emitter& em) {
    const std::vector<std::pair<std::string, std::string>> ops{{"divide", "a / b"}, {"min", "np.minimum(a, b)"}, {"max", "np.maximum(a, b)"}, {"pow", "np.power(a, b)"},
                                                               {"hypot", "np.hypot(a, b)"}};
    std::string e;
    if (uses_variable(io, "op")) {
        std::string table;
        for (const auto& [name, code] : ops) table += (table.empty() ? "" : ", ") + py_str(name) + ": lambda a, b: " + code;
        e = "{" + table + "}[" + em.expr(io, "op") + "](a, b)";
    } else {
        const auto& op = param_text(io.node.params, "op");
        for (const auto& [name, code] : ops)
            if (name == op) e = code;
        if (e.empty()) return std::unexpected{unsupported(io, "op")};
    }
    em.function("pairwise");
    em.function("padded");
    return fmt::format("{0} = pairwise({1}, {2}, lambda u, v: (lambda a, b: {3})(padded(u, max(u.size, v.size)), padded(v, max(u.size, v.size))))", io.out[0], io.in[0], io.in[1], e);
}

core::Result<std::string> repeat(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = np.tile({}, (1, {}))", io.out[0], io.in[0], em.expr(io, "times"));
}

core::Result<std::string> select_channel(const NodeIo& io, Emitter& em) {
    const auto i = em.expr(io, "index");
    return fmt::format("{} = {}[{}:{} + 1]", io.out[0], io.in[0], i, i);
}

core::Result<std::string> channel_delay(const NodeIo& io, Emitter& em) {
    std::string list;
    if (uses_variable(io, "delays")) {
        em.function("number_list");
        list = "[int(d) for d in number_list(" + em.expr(io, "delays") + ")]";
    } else {
        const auto delays = pipeline::blocks::parse_numbers("channel_delay", "delays", param_text(io.node.params, "delays"));
        if (!delays || delays->empty()) return std::unexpected{unsupported(io, "delays")};
        for (const double d : *delays) list += (list.empty() ? "" : ", ") + std::to_string(static_cast<long long>(d));
        list = "[" + list + "]";
    }
    em.function("shifted");
    return fmt::format("{0}_delays = {2}\n{0} = np.stack([shifted(row, {0}_delays[min(c, len({0}_delays) - 1)]) for c, row in enumerate({1})])", io.out[0], io.in[0], list);
}

core::Result<std::string> stereo_width(const NodeIo& io, Emitter& em) {
    return fmt::format("{0} = {1}.copy()\n{0}_mid = ({1}[0] + {1}[1]) / 2\n{0}_side = ({1}[0] - {1}[1]) / 2\n{0}[0] = {0}_mid + {2} * {0}_side\n{0}[1] = {0}_mid - {2} * {0}_side",
                       io.out[0], io.in[0], em.expr(io, "width"));
}

core::Result<std::string> balance(const NodeIo& io, Emitter& em) {
    const auto pos = em.expr(io, "position");
    return fmt::format("{0} = {1}.copy()\n{0}[0] = np.sqrt(1 - {2}) * {1}[0]\n{0}[1] = np.sqrt(1 + {2}) * {1}[1]", io.out[0], io.in[0], pos);
}

core::Result<std::string> sample_hold(const NodeIo& io, Emitter& em) {
    const auto h = samples_expr(io, em, "hold", io.in_fs[0]);
    return fmt::format("{0} = {1}[:, np.arange({1}.shape[1]) - np.arange({1}.shape[1]) % {2}]", io.out[0], io.in[0], h);
}

core::Result<std::string> dc_block(const NodeIo& io, Emitter& em) {
    em.import_line("from scipy import signal");
    return fmt::format("{} = signal.lfilter([1.0, -1.0], [1.0, -{}], {}, axis=1)", io.out[0], em.expr(io, "r"), io.in[0]);
}

core::Result<std::string> add_noise(const NodeIo& io, Emitter& em) {
    em.function("noise_from_draws");
    em.function("portable_random");
    const auto& x = io.in[0];
    return fmt::format("{0}_rms = np.sqrt(np.sum({1} * {1}) / {1}.size / 10 ** ({2} / 10))\n"
                       "{0} = np.stack([noise_from_draws(portable_random({4} + c, {1}.shape[1]), {3}, {0}_rms) for c in range({1}.shape[0])]) + {1}",
                       io.out[0], x, em.expr(io, "snr_db"), em.expr(io, "color"), em.expr(io, "seed"));
}

core::Result<std::string> chirp(const NodeIo& io, Emitter& em) {
    em.function("sweep");
    em.function("samples_count");
    const auto log = uses_variable(io, "method") ? "(" + em.expr(io, "method") + " == \"log\")" : std::string{flag(io, em, "method", "log")};
    const auto fs = em.expr(io, "rate");
    return fmt::format("{} = (sweep({}, {}, {}, {}, samples_count({}, {})) * {})[np.newaxis, :]", io.out[0], em.expr(io, "f0"), em.expr(io, "f1"), log, fs, em.expr(io, "duration"), fs,
                       em.expr(io, "amplitude"));
}

core::Result<std::string> osc(const NodeIo& io, Emitter& em) {
    em.function("osc");
    em.function("samples_count");
    const auto fs = em.expr(io, "rate");
    return fmt::format("{} = (osc({}, {}, {}, {}, {}, samples_count({}, {})) * {})[np.newaxis, :]", io.out[0], em.expr(io, "kind"), em.expr(io, "freq"), em.expr(io, "duty"),
                       em.expr(io, "width"), fs, em.expr(io, "duration"), fs, em.expr(io, "amplitude"));
}

core::Result<std::string> gain(const NodeIo& io, Emitter& em) {
    return fmt::format("{} = {} * 10 ** ({} / 20)", io.out[0], io.in[0], em.expr(io, "db"));
}

core::Result<std::string> wav_out(const NodeIo& io, Emitter& em) {
    const auto& p = io.node.params;
    const auto& x = io.in[0];
    const auto bits = em.expr(io, "bits");
    const auto draws = fmt::format("portable_random({}, {}.size, \"tpdf\")", em.expr(io, "seed"), x);
    std::string dither = "None";
    if (uses_variable(io, "dither") || uses_variable(io, "bits")) {
        em.function("portable_random");
        dither = fmt::format("({} if {} == \"yes\" and {} != \"f32\" else None)", draws, em.expr(io, "dither"), bits);
    } else if (param_text(p, "dither") == "yes" && param_text(p, "bits") != "f32") {
        em.function("portable_random");
        dither = draws;
    }
    em.function("write_wav");
    auto path = pipeline::utf8_path(param_text(p, "path"));
    if (path.is_relative()) path = io.base_dir / path;
    const auto& y = io.out[0];
    return fmt::format("{0}_path = {1}\n{0} = {{\"path\": str({0}_path), \"clipped\": float(write_wav({0}_path, {2}, {3}, {4}, {5}))}}\n{0}_ok = True", y, em.file(path), x, io.in_fs[0],
                       bits, dither);
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
