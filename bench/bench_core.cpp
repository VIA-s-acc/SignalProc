#include <cmath>
#include <random>
#include <string>
#include <vector>

#include <nanobench.h>

#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/wavedec.hpp"

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
    return 0;
}
