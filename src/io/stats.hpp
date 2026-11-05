#pragma once

#include <cstddef>
#include <vector>

#include "io/wav.hpp"

namespace signals::io {

struct ChannelStats {
    double peak_dbfs;
    double rms_dbfs;
    double dc;
    double crest_factor_db;
    std::size_t clipped;
};

std::vector<ChannelStats> channel_stats(const WavFile& file);

}  // namespace signals::io
