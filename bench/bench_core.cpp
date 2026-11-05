#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <nanobench.h>

#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/freq.hpp"
#include "core/multirate.hpp"
#include "core/process/shrink.hpp"
#include "core/wavedec.hpp"
#include "io/wav.hpp"

using namespace signals::core;

int main() {
    std::mt19937_64 rng(5);
    std::normal_distribution<double> g;

    std::vector<double> left(441'000), right(441'000);
    for (auto& v : left) v = g(rng);
    for (auto& v : right) v = g(rng);
    const auto db2 = *wavelet("db2");
    ankerl::nanobench::Bench dwt;
    dwt.title("wavedec db2 J=6 symmetric, 2 x 441000").unit("stereo file").minEpochIterations(3);
    dwt.run("wavedec both channels", [&] {
        auto a = wavedec<double>(left, db2, 6, {});
        auto b = wavedec<double>(right, db2, 6, {});
        ankerl::nanobench::doNotOptimizeAway(a);
        ankerl::nanobench::doNotOptimizeAway(b);
    });
    dwt.run("waverec both channels", [&, ca = *wavedec<double>(left, db2, 6, {}), cb = *wavedec<double>(right, db2, 6, {})] {
        auto a = waverec<double>(ca, db2, {});
        auto b = waverec<double>(cb, db2, {});
        ankerl::nanobench::doNotOptimizeAway(a);
        ankerl::nanobench::doNotOptimizeAway(b);
    });

    Laurent x{std::vector<double>(100'000), 0};
    for (auto& v : x.coeffs) v = g(rng);
    ankerl::nanobench::Bench conv;
    conv.title("convolve N=100000").relative(false).minEpochIterations(5);
    for (std::size_t taps : {8, 16, 32, 64, 128, 256}) {
        Laurent h{std::vector<double>(taps), 0};
        for (auto& v : h.coeffs) v = g(rng);
        conv.run("direct L=" + std::to_string(taps), [&] { ankerl::nanobench::doNotOptimizeAway(convolve(x, h, ConvMethod::direct)); });
        conv.run("fft    L=" + std::to_string(taps), [&] { ankerl::nanobench::doNotOptimizeAway(convolve(x, h, ConvMethod::fft)); });
    }

    ankerl::nanobench::Bench design;
    design.title("filter generation").minEpochIterations(1).epochs(3);
    design.run("wavelet(\"db38\")", [&] { ankerl::nanobench::doNotOptimizeAway(wavelet("db38")); });
    design.run("wavelet(\"sym17\")", [&] { ankerl::nanobench::doNotOptimizeAway(wavelet("sym17")); });

    const auto sym8 = *wavelet("sym8");
    ankerl::nanobench::Bench m2;
    m2.title("M2 processing").minEpochIterations(3);
    m2.run("frequency_response sym8, 8192 points", [&] { ankerl::nanobench::doNotOptimizeAway(frequency_response(sym8.h0, 8192)); });
    const AudioBuffer stereo{44'100, {left, right}};
    m2.run("denoise sym8 bayes, 10 s stereo", [&] { ankerl::nanobench::doNotOptimizeAway(denoise(stereo, {})); });
    m2.run("decimate M=4, 10 s mono", [&] { ankerl::nanobench::doNotOptimizeAway(decimate(left, 4, {})); });

    const auto path = std::filesystem::temp_directory_path() / "signals-bench-10min.wav";
    {
        std::vector<double> l(26'460'000), r(26'460'000);
        for (std::size_t k = 0; k < l.size(); ++k) {
            l[k] = 0.5 * std::sin(0.01 * static_cast<double>(k));
            r[k] = 0.5 * std::cos(0.013 * static_cast<double>(k));
        }
        if (!signals::io::write_wav(path, AudioBuffer{44'100, {std::move(l), std::move(r)}}, {})) return 1;
    }
    ankerl::nanobench::Bench io;
    io.title("WAV").minEpochIterations(1).epochs(3);
    io.run("read_wav 10 min stereo 44.1 kHz s24", [&] { ankerl::nanobench::doNotOptimizeAway(signals::io::read_wav(path)); });
    std::filesystem::remove(path);
    return 0;
}
