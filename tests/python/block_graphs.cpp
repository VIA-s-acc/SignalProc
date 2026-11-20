#include "python/block_graphs.hpp"

#include <algorithm>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "core/progress.hpp"
#include "io/npy.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/npz_values.hpp"
#include "pipeline/sig.hpp"
#include "support/paths.hpp"

namespace signals::test {
namespace {

constexpr const char* kSources =
    "node s1 = gen(kind=harmonics, freq=220, rate=8000, duration=0.25, channels=2, amplitude=0.5)\n"
    "node s2 = gen(kind=white, rate=8000, duration=0.25, channels=2, amplitude=0.1, seed=5)\n"
    "node x = add()\n"
    "s1 -> x.a\n"
    "s2 -> x.b\n"
    "node x2 = gen(kind=sine, freq=330, rate=8000, duration=0.25, channels=2, amplitude=0.4)\n";

struct Producer {
    std::string lines;
    std::vector<std::string> ports;
};

std::string quoted(const std::filesystem::path& p) {
    const auto u = p.generic_u8string();
    return "\"" + std::string{u.begin(), u.end()} + "\"";
}

Producer producer_for(const pipeline::BlockType& t, const std::map<std::string, std::string, std::less<>>& params, const std::string& source) {
    const std::map<std::string, std::string, std::less<>> coeffs{{"iswt", "swt(wavelet=db2, levels=3)"}, {"iwpt", "wpt(wavelet=db2, levels=3)"},
                                                                 {"ilift53", "lift53(levels=3)"},     {"ilift97", "lift97(levels=3)"},
                                                                 {"idtcwt", "dtcwt(levels=3)"}};
    if (t.id == "ipolyphase") return {"node p = polyphase()\nx -> p\n", {"p.even", "p.odd"}};
    if (t.id == "lpc_synth") return {"node l = lpc()\nx -> l\n", {"l.residual", "l.report"}};
    if (t.id == "math") return {"node xp = affine(scale=0.4, offset=1.5)\nx -> xp\n", {"xp"}};
    if (t.id == "binary") return {"node xp = affine(scale=0.4, offset=1.5)\nx -> xp\nnode xq = affine(scale=1, offset=1.5)\nx2 -> xq\n", {"xp", "xq"}};
    if (t.id == "idwt") {
        const auto ext = params.find("ext");
        return {"node c = dwt(wavelet=db2, levels=3, ext=" + (ext == params.end() ? std::string{"symmetric"} : ext->second) + ")\nx -> c\n", {"c"}};
    }
    Producer p;
    std::size_t signals_used = 0;
    for (const auto& port : t.inputs) {
        if (port.type == pipeline::PortType::coeffs) {
            const auto it = coeffs.find(t.id);
            p.lines += "node c = " + (!source.empty() ? source : it == coeffs.end() ? std::string{"dwt(wavelet=db2, levels=3)"} : it->second) + "\nx -> c\n";
            p.ports.emplace_back("c");
        } else if (port.type == pipeline::PortType::report) {
            const auto name = "r" + std::to_string(p.ports.size());
            p.lines += "node " + name + " = stats()\nx -> " + name + "\n";
            p.ports.push_back(name);
        } else {
            p.ports.emplace_back(signals_used++ == 0 ? "x" : "x2");
        }
    }
    return p;
}

std::map<std::string, std::string, std::less<>> path_params(const pipeline::BlockType& t, const std::filesystem::path& dir) {
    if (t.id == "wav") return {{"path", quoted(signals::test::fixture("s24_stereo.wav"))}};
    if (t.id == "coeffs_file") return {{"path", quoted(dir / "coeffs.npz")}};
    if (t.id == "export") return {{"path", quoted(dir / "export.out")}};
    if (t.id == "export_report") return {{"path", quoted(dir / "report.out")}};
    if (t.id == "wav_out") return {{"path", quoted(dir / "out.wav")}};
    return {};
}

std::map<std::string, std::string, std::less<>> base_params(const pipeline::BlockType& t, const std::filesystem::path& dir) {
    auto params = path_params(t, dir);
    if (t.id == "pitch" || t.id == "mfcc" || t.id == "spectral_features") params["frame"] = "512", params["hop"] = "128";
    if (t.id == "time_stretch" || t.id == "pitch_shift") params["n_fft"] = "512", params["hop"] = "128";
    return params;
}

std::string graph_text(const pipeline::BlockType& t, const std::string& name, const std::map<std::string, std::string, std::less<>>& params,
                       const std::string& source = {}) {
    const auto prod = producer_for(t, params, source);
    std::string text = "pipeline \"" + name + "\" v2\n" + kSources + prod.lines + "node b = " + t.id + "(";
    bool first = true;
    for (const auto& [k, v] : params) text += (first ? "" : ", ") + k + "=" + v, first = false;
    text += ")\n";
    for (std::size_t k = 0; k < t.inputs.size() && k < prod.ports.size(); ++k) text += prod.ports[k] + " -> b." + t.inputs[k].name + "\n";
    for (const auto& out : t.outputs) text += "probe b." + out.name + " as \"" + out.name + "\"\n";
    return text;
}

}  // namespace

void prepare_block_files(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    const auto doc = pipeline::parse_sig(std::string{"pipeline \"c\" v2\n"} + kSources + "node c = dwt(wavelet=db2, levels=3)\nx -> c\nprobe c as \"c\"\nprobe x as \"x\"\n");
    REQUIRE(doc);
    const auto graph = pipeline::build_graph(*doc, dir);
    REQUIRE(graph);
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    const auto run = pipeline::run_graph(*graph, cache, {}, cancel);
    REQUIRE(run);
    const auto entries = pipeline::coeffs_entries(std::get<pipeline::Coeffs>(run->probes.at("c")));
    REQUIRE(entries);
    REQUIRE(io::write_npz(dir / "coeffs.npz", *entries));
    const auto& x = std::get<core::AudioBuffer>(run->probes.at("x"));
    REQUIRE(io::write_npz(dir / "only.npz", {{"arr_0", io::NpArray{{x.channels.size(), x.frames()}, [&] {
                                                 std::vector<double> flat;
                                                 for (const auto& c : x.channels) flat.insert(flat.end(), c.begin(), c.end());
                                                 return flat;
                                             }()}}}));
}

std::vector<BlockGraph> block_graphs(const pipeline::BlockType& t, const std::filesystem::path& dir) {
    const auto paths = base_params(t, dir);
    std::vector<BlockGraph> out{{t.id, graph_text(t, t.id, paths)}};
    for (const auto& p : t.params) {
        if (p.kind != pipeline::ParamSpec::Kind::choice) continue;
        for (const auto& value : p.choices) {
            if (value == p.fallback) continue;
            auto params = paths;
            params[p.name] = value;
            if (t.id == "iir" && (value == "bandpass" || value == "bandstop")) params["cutoff"] = "\"500, 1500\"";
            const auto name = t.id + "-" + p.name + "-" + value;
            out.push_back({name, graph_text(t, name, params)});
        }
    }
    if (t.id == "wav") out.push_back({"wav-npz-only", graph_text(t, "wav-npz-only", {{"path", quoted(dir / "only.npz")}, {"rate", "8000"}})});
    if (t.id == "wav")
        for (const char* f : {"s16_stereo.wav", "s32_mono.wav", "u8_mono.wav", "f32_stereo.wav", "f64_mono.wav", "alaw_mono.wav", "mulaw_mono.wav", "s16_6ch_extensible.wav"}) {
            const auto name = std::string{"wav-"} + f;
            out.push_back({name, graph_text(t, name, {{"path", quoted(signals::test::fixture(f))}})});
        }
    if (std::ranges::any_of(t.inputs, [](const auto& port) { return port.optional; })) {
        auto bare = t;
        std::erase_if(bare.inputs, [](const auto& port) { return port.optional; });
        auto text = graph_text(bare, t.id + "-bare", paths);
        out.push_back({t.id + "-bare", std::move(text)});
    }
    const bool generic_coeffs = std::ranges::any_of(t.inputs, [](const auto& port) { return port.type == pipeline::PortType::coeffs; }) && !t.id.starts_with("i");
    if (generic_coeffs)
        for (const char* source : {"swt(wavelet=db2, levels=3)", "wpt(wavelet=db2, levels=3)", "lift53(levels=3)", "lift97(levels=3)", "dtcwt(levels=3)"}) {
            const std::string kind{source, std::string_view{source}.find('(')};
            out.push_back({t.id + "-from-" + kind, graph_text(t, t.id + "-from-" + kind, paths, source)});
        }
    if (t.id == "correlation") {
        auto text = graph_text(t, "correlation-names", paths);
        text += "node s = stats()\nx -> s\nnode s_ok = gain(db=-1)\nx -> s_ok\nnode io = gain(db=-2)\nx -> io\nnode display = gain(db=-3)\nx -> display\n"
                "node sp_stats = gain(db=-4)\nx -> sp_stats\nnode stats = gain(db=-5)\nx -> stats\nnode x_v = gain(db=-6)\nx -> x_v\n"
                "probe s_ok as \"a\"\nprobe io as \"b\"\nprobe display as \"c\"\nprobe sp_stats as \"d\"\nprobe stats as \"e\"\nprobe x_v as \"f\"\n";
        out.push_back({"correlation-names", std::move(text)});
    }
    if (t.id == "gain" || t.id == "assert" || t.id == "threshold") {
        auto text = graph_text(t, t.id + "-bypass", paths);
        const auto at = text.find("node b = ");
        const auto end = text.find('\n', at);
        text.insert(end, " bypass");
        out.push_back({t.id + "-bypass", std::move(text)});
    }
    if (t.id == "sst_band" || t.id == "sst_ridge") {
        std::string text = "pipeline \"" + t.id + "-long\" v2\nnode x = gen(kind=harmonics, freq=220, rate=8000, duration=5)\nnode b = " + t.id + "()\nx -> b\n";
        for (const auto& port : t.outputs) text += "probe b." + port.name + " as \"" + port.name + "\"\n";
        out.push_back({t.id + "-long", std::move(text)});
    }
    if (t.id == "assert")
        out.push_back({"assert-ops", graph_text(t, "assert-ops", {{"expr", "\"a.rms_ch0 / 0 > 1 && (0 - 8) ^ 0.5 != 1 && 10 ^ 400 > a.passed && sqrt(0 - 1) != 0 && log10(0) < 0 && pi > 3 && "
                                                                          "(-a.rms_ch0 + +b.peak_ch1 * 2 / 3 - a.mean_ch0 ^ 2 >= 0.01 && !(c.std_ch0 == 0) || "
                                                                          "abs(d.mean_ch1) != sqrt(log10(100)) && min(a.rms_ch0, 1, 2) <= max(b.rms_ch1, 0))\""}})});
    return out;
}

}  // namespace signals::test
