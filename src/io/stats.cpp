#include "io/stats.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/metrics.hpp"

namespace signals::io {
namespace {

double clip_level(SampleFormat format) {
    switch (format) {
    case SampleFormat::pcm_u8: return 127.0 / 128;
    case SampleFormat::pcm_s16: return 32767.0 / 32768;
    case SampleFormat::pcm_s24: return 8388607.0 / 8388608;
    case SampleFormat::pcm_s32: return 2147483647.0 / 2147483648;
    case SampleFormat::mulaw: return 32124.0 / 32768;
    case SampleFormat::alaw: return 32256.0 / 32768;
    case SampleFormat::float32:
    case SampleFormat::float64: return 1.0;
    }
    return 1.0;
}

double to_db(double amplitude) {
    return amplitude > 0 ? 20 * std::log10(amplitude) : -std::numeric_limits<double>::infinity();
}

}  // namespace

std::vector<ChannelStats> channel_stats(const WavFile& file) {
    const double level = clip_level(file.info.format);
    std::vector<ChannelStats> out;
    out.reserve(file.audio.channels.size());
    for (const auto& x : file.audio.channels) {
        double peak = 0;
        std::size_t clipped = 0;
        for (double v : x) {
            peak = std::max(peak, std::abs(v));
            if (std::abs(v) >= level) ++clipped;
        }
        const double n = static_cast<double>(x.size());
        const double rms = x.empty() ? 0.0 : std::sqrt(core::energy(x) / n);
        const double dc = x.empty() ? 0.0 : core::neumaier_sum(x) / n;
        const double peak_db = to_db(peak), rms_db = to_db(rms);
        out.push_back({peak_db, rms_db, dc, peak > 0 ? peak_db - rms_db : 0.0, clipped});
    }
    return out;
}

}  // namespace signals::io
