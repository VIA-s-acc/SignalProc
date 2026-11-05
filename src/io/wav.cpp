#include "io/wav.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <dr_wav.h>

namespace signals::io {
namespace {

using core::Errc;
using core::make_error;

constexpr drwav_uint64 kBlockFrames = 1 << 16;
constexpr std::size_t kMaxReserveFrames = std::size_t{1} << 24;

struct DrWavGuard {
    drwav& wav;
    ~DrWavGuard() { drwav_uninit(&wav); }
};

std::optional<SampleFormat> sample_format(const drwav& wav) {
    switch (wav.translatedFormatTag) {
    case DR_WAVE_FORMAT_PCM:
        switch (wav.bitsPerSample) {
        case 8: return SampleFormat::pcm_u8;
        case 16: return SampleFormat::pcm_s16;
        case 24: return SampleFormat::pcm_s24;
        case 32: return SampleFormat::pcm_s32;
        default: return std::nullopt;
        }
    case DR_WAVE_FORMAT_IEEE_FLOAT:
        switch (wav.bitsPerSample) {
        case 32: return SampleFormat::float32;
        case 64: return SampleFormat::float64;
        default: return std::nullopt;
        }
    case DR_WAVE_FORMAT_ALAW: return SampleFormat::alaw;
    case DR_WAVE_FORMAT_MULAW: return SampleFormat::mulaw;
    default: return std::nullopt;
    }
}

std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 | std::uint32_t{p[3]} << 24;
}

std::uint64_t le64(const unsigned char* p) { return std::uint64_t{le32(p)} | std::uint64_t{le32(p + 4)} << 32; }

double decode_pcm_u8(const unsigned char* p) { return (static_cast<double>(p[0]) - 128.0) / 128.0; }

double decode_pcm_s16(const unsigned char* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | p[1] << 8)) / 32768.0;
}

double decode_pcm_s24(const unsigned char* p) {
    auto v = static_cast<std::int32_t>(std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16);
    if (v & 0x800000) v -= 0x1000000;
    return v / 8388608.0;
}

double decode_pcm_s32(const unsigned char* p) { return static_cast<std::int32_t>(le32(p)) / 2147483648.0; }

double decode_float32(const unsigned char* p) { return std::bit_cast<float>(le32(p)); }

double decode_float64(const unsigned char* p) { return std::bit_cast<double>(le64(p)); }

template <class Decode>
void read_raw(drwav& wav, std::size_t bytes_per_sample, Decode decode, std::vector<std::vector<double>>& channels) {
    const std::size_t nch = channels.size();
    std::vector<unsigned char> block(kBlockFrames * nch * bytes_per_sample);
    while (const auto got = drwav_read_pcm_frames(&wav, kBlockFrames, block.data())) {
        const unsigned char* p = block.data();
        for (drwav_uint64 f = 0; f < got; ++f)
            for (std::size_t c = 0; c < nch; ++c, p += bytes_per_sample) channels[c].push_back(decode(p));
    }
}

void read_g711(drwav& wav, std::vector<std::vector<double>>& channels) {
    const std::size_t nch = channels.size();
    std::vector<drwav_int16> block(kBlockFrames * nch);
    while (const auto got = drwav_read_pcm_frames_s16(&wav, kBlockFrames, block.data())) {
        const drwav_int16* p = block.data();
        for (drwav_uint64 f = 0; f < got; ++f)
            for (std::size_t c = 0; c < nch; ++c) channels[c].push_back(*p++ / 32768.0);
    }
}

void read_samples(drwav& wav, SampleFormat format, std::vector<std::vector<double>>& channels) {
    switch (format) {
    case SampleFormat::pcm_u8: return read_raw(wav, 1, decode_pcm_u8, channels);
    case SampleFormat::pcm_s16: return read_raw(wav, 2, decode_pcm_s16, channels);
    case SampleFormat::pcm_s24: return read_raw(wav, 3, decode_pcm_s24, channels);
    case SampleFormat::pcm_s32: return read_raw(wav, 4, decode_pcm_s32, channels);
    case SampleFormat::float32: return read_raw(wav, 4, decode_float32, channels);
    case SampleFormat::float64: return read_raw(wav, 8, decode_float64, channels);
    case SampleFormat::alaw:
    case SampleFormat::mulaw: return read_g711(wav, channels);
    }
}

}  // namespace

std::string_view to_string(SampleFormat format) {
    switch (format) {
    case SampleFormat::pcm_u8: return "pcm_u8";
    case SampleFormat::pcm_s16: return "pcm_s16";
    case SampleFormat::pcm_s24: return "pcm_s24";
    case SampleFormat::pcm_s32: return "pcm_s32";
    case SampleFormat::float32: return "float32";
    case SampleFormat::float64: return "float64";
    case SampleFormat::alaw: return "alaw";
    case SampleFormat::mulaw: return "mulaw";
    }
    return "unknown";
}

core::Result<WavFile> read_wav(const std::filesystem::path& path) {
    drwav wav;
    if (!drwav_init_file(&wav, path.string().c_str(), nullptr))
        return make_error(Errc::io_error, "cannot open or parse WAV file: " + path.string());
    DrWavGuard guard{wav};

    const auto format = sample_format(wav);
    if (!format)
        return make_error(Errc::invalid_argument, "unsupported WAV format: tag " + std::to_string(wav.translatedFormatTag) +
                                                      ", " + std::to_string(wav.bitsPerSample) + " bits");
    if (wav.channels == 0) return make_error(Errc::invalid_argument, "WAV file has no channels: " + path.string());

    std::vector<std::vector<double>> channels(wav.channels);
    const auto reserve = static_cast<std::size_t>(std::min<drwav_uint64>(wav.totalPCMFrameCount, kMaxReserveFrames));
    for (auto& c : channels) c.reserve(reserve);
    read_samples(wav, *format, channels);
    if (channels.front().empty()) return make_error(Errc::invalid_argument, "empty WAV: " + path.string());

    WavInfo info{*format, wav.channels, wav.sampleRate, channels.front().size(), wav.fmt.formatTag == DR_WAVE_FORMAT_EXTENSIBLE};
    return WavFile{info, core::AudioBuffer{static_cast<double>(wav.sampleRate), std::move(channels)}};
}

}  // namespace signals::io
