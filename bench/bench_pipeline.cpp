#include "bench_pipeline.hpp"

#include <chrono>
#include <cstdio>
#include <string>

#include <nanobench.h>

#include "core/process/shrink.hpp"
#include "io/wav.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/sig.hpp"

namespace {

using namespace signals;

pipeline::Graph graph_of(const std::string& text, const std::filesystem::path& dir) {
    auto doc = pipeline::parse_sig(text);
    if (!doc) {
        std::fprintf(stderr, "bench: %s\n", doc.error().message.c_str());
        std::exit(1);
    }
    auto g = pipeline::build_graph(*doc, dir);
    if (!g) {
        std::fprintf(stderr, "bench: %s\n", g.error().message.c_str());
        std::exit(1);
    }
    return *g;
}

}  // namespace

void bench_pipeline(const std::filesystem::path& ten_minutes) {
    std::string chain = "node n0 = gen(duration=0.01)\n";
    for (int i = 1; i < 20; ++i) chain += "node n" + std::to_string(i) + " = gain(db=-0.1)\n";
    for (int i = 1; i < 20; ++i) chain += "n" + std::to_string(i - 1) + " -> n" + std::to_string(i) + "\n";
    const auto small = graph_of(chain, ".");
    core::CancelToken cancel;
    double overhead_ms = 1e9;
    for (int round = 0; round < 20; ++round) {
        pipeline::ResultCache none{0};
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = pipeline::run_graph(small, none, {}, cancel);
        const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        double blocks = 0;
        for (const auto& s : r->nodes) blocks += s.wall_seconds * 1e3;
        overhead_ms = std::min(overhead_ms, total - blocks);
    }
    std::printf("pipeline overhead, 20 nodes on 441 samples: %.3f ms over the blocks (goal < 1 ms)\n", overhead_ms);

    ankerl::nanobench::Bench b;
    b.title("pipeline").minEpochIterations(1).epochs(3);
    const auto dir = ten_minutes.parent_path();
    const auto denoise = graph_of("node src = wav(path=\"" + ten_minutes.filename().string() +
                                      "\")\nnode dwt = dwt(wavelet=sym8, levels=0)\nnode thr = threshold()\nnode idwt = idwt(wavelet=sym8)\n"
                                      "node out = wav_out(path=\"signals-bench-out.wav\", dither=none)\nsrc -> dwt -> thr -> idwt -> out\n",
                                  dir);
    b.run("denoise.sig, 10 min stereo (read, dwt, threshold, idwt, write)", [&] {
        pipeline::ResultCache none{0};
        ankerl::nanobench::doNotOptimizeAway(pipeline::run_graph(denoise, none, {}, cancel));
    });
    b.run("signals denoise path, 10 min stereo (read, denoise, write)", [&] {
        auto file = io::read_wav(ten_minutes);
        auto result = core::denoise(file->audio, {});
        io::WavWriteOptions o;
        o.dither = false;
        ankerl::nanobench::doNotOptimizeAway(io::write_wav(dir / "signals-bench-out.wav", result->first, o));
    });

    const auto pure = graph_of("node src = gen(duration=10)\nnode dwt = dwt(wavelet=sym8, levels=6)\nnode thr = threshold()\nnode idwt = idwt(wavelet=sym8)\n"
                               "src -> dwt -> thr -> idwt\nprobe idwt as \"out\"\n",
                               ".");
    pipeline::ResultCache cache{std::size_t{1} << 31};
    (void)pipeline::run_graph(pure, cache, {}, cancel);
    double cached_ms = 1e9;
    for (int round = 0; round < 20; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        ankerl::nanobench::doNotOptimizeAway(pipeline::run_graph(pure, cache, {}, cancel));
        cached_ms = std::min(cached_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::printf("pipeline rerun from cache, 4 nodes on 10 s: %.3f ms (goal < 5 ms)\n", cached_ms);
    std::filesystem::remove(dir / "signals-bench-out.wav");
}
