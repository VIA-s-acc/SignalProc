#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/signal_ops.hpp"
#include "io/generators.hpp"
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
    else if (kind == "sweep") em.function("sweep_log"), x = fmt::format("sweep_log({}, {}, {}, {})", em.number(f), em.number(param_real(p, "f1")), fs, num);
    else if (kind == "impulse") x = fmt::format("(np.arange({}) == 0) * 1.0", num);
    else if (kind == "step") x = fmt::format("np.ones({})", num);
    else if (kind == "harmonics") {
        std::string sum;
        for (int h = 0; h < 4; ++h)
            sum += (h ? " + " : "") + fmt::format("{} * np.sin(2 * np.pi * {} * np.arange({}) / {})", em.number(a / std::ldexp(1.0, h)), em.number((h + 1) * f), num, fs);
        x = sum;
    } else {
        em.import_line("import pywt");
        em.import_line("import pywt.data");
        const char* name = kind == "doppler" ? "Doppler" : kind == "blocks" ? "Blocks" : kind == "bumps" ? "Bumps" : kind == "heavisine" ? "HeaviSine" : nullptr;
        if (!name) return std::unexpected{unsupported(io, "kind")};
        x = fmt::format("pywt.data.demo_signal({}, {}) * {}", py_str(name), num, em.number(a));
    }
    return fmt::format("{} = np.tile({}, ({}, 1))", y, x, channels);
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
    out.push_back({"gen", 1e-12, gen});
    out.push_back({"gain", 1e-12, gain});
    out.push_back({"wav_out", 1e-12, wav_out});
}

}  // namespace signals::python
