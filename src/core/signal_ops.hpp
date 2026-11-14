#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/error.hpp"

namespace signals::core {

std::vector<double> gain_db(std::span<const double> x, double db);
std::vector<double> normalize_peak(std::span<const double> x, double target_dbfs);
std::vector<double> normalize_rms(std::span<const double> x, double target_dbfs);
std::vector<double> remove_dc(std::span<const double> x);
std::vector<double> invert(std::span<const double> x);

struct ClipResult {
    std::vector<double> samples;
    std::size_t clipped = 0;
};
ClipResult hard_clip(std::span<const double> x, double limit);
class TpdfDither {
public:
    explicit TpdfDither(std::uint64_t seed) : rng_{seed} {}
    double next() {
        const double d1 = uniform_(rng_);
        const double d2 = uniform_(rng_);
        return d1 - d2;
    }

private:
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
};

std::vector<double> tpdf_draws(std::size_t n, std::uint64_t seed);
Result<std::vector<double>> quantize(std::span<const double> x, int bits, bool dither, std::uint64_t seed);
std::vector<double> mulaw_encode(std::span<const double> x, double mu = 255);
std::vector<double> mulaw_decode(std::span<const double> y, double mu = 255);

Result<AudioBuffer> to_mid_side(const AudioBuffer& audio);
Result<AudioBuffer> from_mid_side(const AudioBuffer& audio);
AudioBuffer mixdown(const AudioBuffer& audio);
Result<AudioBuffer> select_channel(const AudioBuffer& audio, std::size_t index);

std::vector<double> delay(std::span<const double> x, std::size_t samples);
std::vector<double> reverse(std::span<const double> x);
Result<std::vector<double>> cut(std::span<const double> x, std::size_t begin, std::size_t end);

}  // namespace signals::core
