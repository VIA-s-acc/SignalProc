#include "io/wav.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <dr_wav.h>

namespace signals::io {
namespace {

using core::Errc;
using core::make_error;

constexpr drwav_uint64 kBlockFrames = 1 << 16;
constexpr std::size_t kMaxChannels = 256;
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

bool keep_going(core::ProgressSink* progress, std::size_t done, drwav_uint64 total) {
    if (!progress) return true;
    progress->report(total ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total)) : 1.0);
    return !progress->cancelled();
}

template <class Decode>
bool read_raw(drwav& wav, std::size_t bytes_per_sample, Decode decode, std::vector<std::vector<double>>& channels,
              core::ProgressSink* progress) {
    const std::size_t nch = channels.size();
    std::vector<unsigned char> block(kBlockFrames * nch * bytes_per_sample);
    while (const auto got = drwav_read_pcm_frames(&wav, kBlockFrames, block.data())) {
        const unsigned char* p = block.data();
        for (drwav_uint64 f = 0; f < got; ++f)
            for (std::size_t c = 0; c < nch; ++c, p += bytes_per_sample) channels[c].push_back(decode(p));
        if (!keep_going(progress, channels.front().size(), wav.totalPCMFrameCount)) return false;
    }
    return true;
}

bool read_g711(drwav& wav, std::vector<std::vector<double>>& channels, core::ProgressSink* progress) {
    const std::size_t nch = channels.size();
    std::vector<drwav_int16> block(kBlockFrames * nch);
    while (const auto got = drwav_read_pcm_frames_s16(&wav, kBlockFrames, block.data())) {
        const drwav_int16* p = block.data();
        for (drwav_uint64 f = 0; f < got; ++f)
            for (std::size_t c = 0; c < nch; ++c) channels[c].push_back(*p++ / 32768.0);
        if (!keep_going(progress, channels.front().size(), wav.totalPCMFrameCount)) return false;
    }
    return true;
}

bool read_samples(drwav& wav, SampleFormat format, std::vector<std::vector<double>>& channels, core::ProgressSink* progress) {
    switch (format) {
    case SampleFormat::pcm_u8: return read_raw(wav, 1, decode_pcm_u8, channels, progress);
    case SampleFormat::pcm_s16: return read_raw(wav, 2, decode_pcm_s16, channels, progress);
    case SampleFormat::pcm_s24: return read_raw(wav, 3, decode_pcm_s24, channels, progress);
    case SampleFormat::pcm_s32: return read_raw(wav, 4, decode_pcm_s32, channels, progress);
    case SampleFormat::float32: return read_raw(wav, 4, decode_float32, channels, progress);
    case SampleFormat::float64: return read_raw(wav, 8, decode_float64, channels, progress);
    case SampleFormat::alaw:
    case SampleFormat::mulaw: return read_g711(wav, channels, progress);
    }
    return true;
}

struct WriteEncoding {
    drwav_uint32 tag;
    drwav_uint32 bits;
};

std::optional<WriteEncoding> write_encoding(SampleFormat format) {
    switch (format) {
    case SampleFormat::pcm_s16: return WriteEncoding{DR_WAVE_FORMAT_PCM, 16};
    case SampleFormat::pcm_s24: return WriteEncoding{DR_WAVE_FORMAT_PCM, 24};
    case SampleFormat::pcm_s32: return WriteEncoding{DR_WAVE_FORMAT_PCM, 32};
    case SampleFormat::float32: return WriteEncoding{DR_WAVE_FORMAT_IEEE_FLOAT, 32};
    default: return std::nullopt;
    }
}

core::Result<void> validate_for_write(const core::AudioBuffer& audio) {
    if (audio.channels.empty() || audio.channels.size() > kMaxChannels)
        return make_error(Errc::invalid_argument, "WAV needs 1 to " + std::to_string(kMaxChannels) + " channels");
    const auto frames = audio.frames();
    for (const auto& c : audio.channels)
        if (c.size() != frames) return make_error(Errc::invalid_argument, "channels differ in length");
    const double fs = audio.sample_rate;
    if (!(fs >= 1.0 && fs <= 4294967295.0 && fs == std::floor(fs)))
        return make_error(Errc::invalid_argument, "sample rate must be a positive integer");
    for (std::size_t c = 0; c < audio.channels.size(); ++c)
        for (std::size_t k = 0; k < frames; ++k)
            if (!std::isfinite(audio.channels[c][k]))
                return make_error(Errc::invalid_argument, "non-finite sample in channel " + std::to_string(c) +
                                                              " at frame " + std::to_string(k));
    return {};
}

class Quantizer {
public:
    Quantizer(const WriteEncoding& encoding, const WavWriteOptions& options)
        : is_float_{encoding.tag == DR_WAVE_FORMAT_IEEE_FLOAT},
          bytes_{encoding.bits / 8},
          scale_{std::ldexp(1.0, static_cast<int>(encoding.bits) - 1)},
          dither_{options.dither},
          rng_{options.dither_seed} {}

    std::size_t bytes() const { return bytes_; }
    std::size_t clipped() const { return clipped_; }

    void encode(double x, unsigned char* out) {
        if (is_float_) return put(std::bit_cast<std::uint32_t>(static_cast<float>(x)), out);
        double v = x * scale_;
        if (dither_) {
            const double d1 = uniform_(rng_);
            const double d2 = uniform_(rng_);
            v += d1 - d2;
        }
        double q = std::round(v);
        if (q < -scale_) {
            q = -scale_;
            ++clipped_;
        } else if (q > scale_ - 1.0) {
            q = scale_ - 1.0;
            ++clipped_;
        }
        put(static_cast<std::uint64_t>(static_cast<std::int64_t>(q)), out);
    }

private:
    void put(std::uint64_t v, unsigned char* out) const {
        for (std::size_t b = 0; b < bytes_; ++b) out[b] = static_cast<unsigned char>(v >> (8 * b));
    }

    bool is_float_;
    std::size_t bytes_;
    double scale_;
    bool dither_;
    std::size_t clipped_ = 0;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
};

core::Result<WavWriteReport> write_failed(drwav* wav, const std::filesystem::path& path, bool remove_file) {
    if (wav) drwav_uninit(wav);
    std::error_code ignored;
    if (remove_file) std::filesystem::remove(path, ignored);
    return make_error(Errc::io_error, "cannot write WAV file: " + path.string());
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

core::Result<WavFile> read_wav(const std::filesystem::path& path) { return read_wav(path, nullptr); }

core::Result<WavFile> read_wav(const std::filesystem::path& path, core::ProgressSink* progress) {
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
    if (!read_samples(wav, *format, channels, progress)) return make_error(Errc::cancelled, "reading cancelled: " + path.string());
    if (channels.front().empty()) return make_error(Errc::invalid_argument, "empty WAV: " + path.string());

    WavInfo info{*format, wav.channels, wav.sampleRate, channels.front().size(), wav.fmt.formatTag == DR_WAVE_FORMAT_EXTENSIBLE};
    return WavFile{info, core::AudioBuffer{static_cast<double>(wav.sampleRate), std::move(channels)}};
}

core::Result<WavWriteReport> write_wav(const std::filesystem::path& path, const core::AudioBuffer& audio,
                                       const WavWriteOptions& options) {
    const auto encoding = write_encoding(options.format);
    if (!encoding)
        return make_error(Errc::invalid_argument, "cannot write WAV as " + std::string{to_string(options.format)});
    if (auto ok = validate_for_write(audio); !ok) return std::unexpected{ok.error()};

    const auto nch = audio.channels.size();
    drwav_data_format format{drwav_container_riff, encoding->tag, static_cast<drwav_uint32>(nch),
                             static_cast<drwav_uint32>(audio.sample_rate), encoding->bits};
    std::error_code ec;
    const bool existed = std::filesystem::exists(path, ec);
    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) return write_failed(nullptr, path, !existed);

    Quantizer quantizer{*encoding, options};
    const auto frames = audio.frames();
    std::vector<unsigned char> block(kBlockFrames * nch * quantizer.bytes());
    for (std::size_t start = 0; start < frames; start += kBlockFrames) {
        const auto n = std::min<std::size_t>(kBlockFrames, frames - start);
        unsigned char* p = block.data();
        for (std::size_t f = start; f < start + n; ++f)
            for (std::size_t c = 0; c < nch; ++c, p += quantizer.bytes()) quantizer.encode(audio.channels[c][f], p);
        if (drwav_write_pcm_frames(&wav, n, block.data()) != n) return write_failed(&wav, path, true);
    }
    if (drwav_uninit(&wav) != DRWAV_SUCCESS) return write_failed(nullptr, path, true);
    return WavWriteReport{quantizer.clipped()};
}

}  // namespace signals::io
