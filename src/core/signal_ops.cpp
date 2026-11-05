#include "core/signal_ops.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "core/metrics.hpp"

namespace signals::core {
namespace {

std::vector<double> scaled(std::span<const double> x, double factor) {
    std::vector<double> y(x.begin(), x.end());
    for (double& v : y) v *= factor;
    return y;
}

double db_to_amplitude(double db) { return std::pow(10.0, db / 20.0); }

Result<void> require_stereo(const AudioBuffer& audio) {
    if (audio.channels.size() != 2) return make_error(Errc::invalid_argument, "mid/side needs exactly 2 channels");
    if (audio.channels[0].size() != audio.channels[1].size())
        return make_error(Errc::invalid_argument, "channels differ in length");
    return {};
}

AudioBuffer rotate_stereo(const AudioBuffer& audio) {
    constexpr double c = std::numbers::sqrt2 / 2;
    AudioBuffer out{audio.sample_rate, {audio.channels[0], audio.channels[1]}};
    for (std::size_t k = 0; k < audio.frames(); ++k) {
        const double a = audio.channels[0][k], b = audio.channels[1][k];
        out.channels[0][k] = (a + b) * c;
        out.channels[1][k] = (a - b) * c;
    }
    return out;
}

}  // namespace

std::vector<double> gain_db(std::span<const double> x, double db) { return scaled(x, db_to_amplitude(db)); }

std::vector<double> normalize_peak(std::span<const double> x, double target_dbfs) {
    double peak = 0;
    for (double v : x) peak = std::max(peak, std::abs(v));
    if (peak == 0) return {x.begin(), x.end()};
    return scaled(x, db_to_amplitude(target_dbfs) / peak);
}

std::vector<double> normalize_rms(std::span<const double> x, double target_dbfs) {
    const double rms = x.empty() ? 0.0 : std::sqrt(energy(x) / static_cast<double>(x.size()));
    if (rms == 0) return {x.begin(), x.end()};
    return scaled(x, db_to_amplitude(target_dbfs) / rms);
}

std::vector<double> remove_dc(std::span<const double> x) {
    std::vector<double> y(x.begin(), x.end());
    if (y.empty()) return y;
    const double mean = neumaier_sum(x) / static_cast<double>(x.size());
    for (double& v : y) v -= mean;
    return y;
}

std::vector<double> invert(std::span<const double> x) { return scaled(x, -1.0); }

ClipResult hard_clip(std::span<const double> x, double limit) {
    ClipResult r{{x.begin(), x.end()}, 0};
    for (double& v : r.samples) {
        if (std::abs(v) > limit) {
            v = std::copysign(limit, v);
            ++r.clipped;
        }
    }
    return r;
}

Result<AudioBuffer> to_mid_side(const AudioBuffer& audio) {
    if (auto ok = require_stereo(audio); !ok) return std::unexpected{ok.error()};
    return rotate_stereo(audio);
}

Result<AudioBuffer> from_mid_side(const AudioBuffer& audio) {
    if (auto ok = require_stereo(audio); !ok) return std::unexpected{ok.error()};
    return rotate_stereo(audio);
}

AudioBuffer mixdown(const AudioBuffer& audio) {
    AudioBuffer out{audio.sample_rate, {}};
    if (audio.channels.empty()) return out;
    std::vector<double> sum(audio.frames(), 0.0);
    for (const auto& c : audio.channels)
        for (std::size_t k = 0; k < sum.size() && k < c.size(); ++k) sum[k] += c[k];
    for (double& v : sum) v /= static_cast<double>(audio.channels.size());
    out.channels.push_back(std::move(sum));
    return out;
}

Result<AudioBuffer> select_channel(const AudioBuffer& audio, std::size_t index) {
    if (index >= audio.channels.size())
        return make_error(Errc::invalid_argument, "channel " + std::to_string(index) + " does not exist (" +
                                                      std::to_string(audio.channels.size()) + " channels)");
    return AudioBuffer{audio.sample_rate, {audio.channels[index]}};
}

std::vector<double> delay(std::span<const double> x, std::size_t samples) {
    std::vector<double> y(samples, 0.0);
    y.insert(y.end(), x.begin(), x.end());
    return y;
}

std::vector<double> reverse(std::span<const double> x) { return {x.rbegin(), x.rend()}; }

Result<std::vector<double>> cut(std::span<const double> x, std::size_t begin, std::size_t end) {
    if (begin > end || end > x.size())
        return make_error(Errc::invalid_argument, "cut: need begin <= end <= " + std::to_string(x.size()));
    return std::vector<double>(x.begin() + static_cast<std::ptrdiff_t>(begin), x.begin() + static_cast<std::ptrdiff_t>(end));
}

}  // namespace signals::core
