#include "io/audio_file.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <dr_flac.h>
#include <dr_mp3.h>

namespace signals::io {
namespace {

using core::Errc;
using core::make_error;

constexpr std::size_t kBlockFrames = std::size_t{1} << 16;
constexpr std::size_t kMaxReserveFrames = std::size_t{1} << 24;

enum class Sniff { missing, wav, flac, mp3, unknown };

Sniff sniff(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) return Sniff::missing;
    std::array<unsigned char, 4> b{};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (in.gcount() < 4) return Sniff::unknown;
    const auto is = [&](const char* tag) { return b[0] == tag[0] && b[1] == tag[1] && b[2] == tag[2] && b[3] == tag[3]; };
    if (is("RIFF") || is("RF64") || is("BW64") || is("riff")) return Sniff::wav;
    if (is("fLaC") || is("OggS")) return Sniff::flac;
    if (b[0] == 'I' && b[1] == 'D' && b[2] == '3') return Sniff::mp3;
    const bool sync = b[0] == 0xFF && (b[1] & 0xE0) == 0xE0, layer = (b[1] & 0x06) != 0, bitrate = (b[2] & 0xF0) != 0xF0, rate = (b[2] & 0x0C) != 0x0C;
    return sync && layer && bitrate && rate ? Sniff::mp3 : Sniff::unknown;
}

core::Error unsupported(const std::filesystem::path& path) {
    return {Errc::invalid_argument, "not a WAV, FLAC or MP3 file: " + path.string()};
}

SampleFormat format_for_bits(unsigned bits) {
    if (bits <= 16) return SampleFormat::pcm_s16;
    if (bits <= 24) return SampleFormat::pcm_s24;
    return SampleFormat::pcm_s32;
}

bool keep_going(core::ProgressSink* progress, std::size_t done, std::uint64_t total) {
    if (!progress) return true;
    progress->report(total ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total)) : 1.0);
    return !progress->cancelled();
}

drflac* open_flac(const std::filesystem::path& path) {
#ifdef _WIN32
    return drflac_open_file_w(path.c_str(), nullptr);
#else
    return drflac_open_file(path.c_str(), nullptr);
#endif
}

bool open_mp3(drmp3& mp3, const std::filesystem::path& path) {
#ifdef _WIN32
    return drmp3_init_file_w(&mp3, path.c_str(), nullptr);
#else
    return drmp3_init_file(&mp3, path.c_str(), nullptr);
#endif
}

struct FlacGuard {
    drflac* flac;
    ~FlacGuard() { drflac_close(flac); }
};

struct Mp3Guard {
    drmp3& mp3;
    ~Mp3Guard() { drmp3_uninit(&mp3); }
};

WavInfo flac_info(const drflac& f, std::uint64_t frames) {
    return WavInfo{format_for_bits(f.bitsPerSample), f.channels, f.sampleRate, frames, false, Container::flac, f.bitsPerSample};
}

std::optional<std::uint64_t> flac_frames(drflac& f) {
    if (f.totalPCMFrameCount > 0) return f.totalPCMFrameCount;
    std::vector<drflac_int32> scratch(kBlockFrames * f.channels);
    std::uint64_t frames = 0;
    for (drflac_uint64 got; (got = drflac_read_pcm_frames_s32(&f, kBlockFrames, scratch.data())) > 0;) frames += got;
    if (!drflac_seek_to_pcm_frame(&f, 0)) return std::nullopt;
    return frames;
}

core::Result<WavFile> read_flac(const std::filesystem::path& path, core::ProgressSink* progress) {
    auto* f = open_flac(path);
    if (!f) return make_error(Errc::invalid_argument, "cannot parse FLAC file: " + path.string());
    FlacGuard guard{f};
    if (f->channels == 0) return make_error(Errc::invalid_argument, "FLAC file has no channels: " + path.string());
    const auto total = flac_frames(*f);
    if (!total) return make_error(Errc::io_error, "cannot rewind FLAC file: " + path.string());
    std::vector<std::vector<double>> channels(f->channels);
    const auto reserve = static_cast<std::size_t>(std::min<std::uint64_t>(*total, kMaxReserveFrames));
    for (auto& c : channels) c.reserve(reserve);
    std::vector<drflac_int32> block(kBlockFrames * f->channels);
    for (std::uint64_t done = 0; done < *total;) {
        const auto want = std::min<std::uint64_t>(kBlockFrames, *total - done);
        const auto got = static_cast<std::size_t>(drflac_read_pcm_frames_s32(f, want, block.data()));
        for (std::size_t n = 0; n < got; ++n)
            for (std::size_t c = 0; c < channels.size(); ++c) channels[c].push_back(block[n * channels.size() + c] / 2147483648.0);
        done += got;
        if (got > 0 && !keep_going(progress, done, *total)) return make_error(Errc::cancelled, "reading cancelled: " + path.string());
        if (got < want) break;
    }
    if (channels.front().empty()) return make_error(Errc::invalid_argument, "empty FLAC: " + path.string());
    auto info = flac_info(*f, channels.front().size());
    return WavFile{info, core::AudioBuffer{static_cast<double>(f->sampleRate), std::move(channels)}};
}

core::Result<WavFile> read_mp3(const std::filesystem::path& path, core::ProgressSink* progress) {
    drmp3 mp3;
    if (!open_mp3(mp3, path)) return make_error(Errc::invalid_argument, "cannot parse MP3 file: " + path.string());
    Mp3Guard guard{mp3};
    if (mp3.channels == 0) return make_error(Errc::invalid_argument, "MP3 file has no channels: " + path.string());
    const std::uint64_t total = drmp3_get_pcm_frame_count(&mp3);
    if (!drmp3_seek_to_pcm_frame(&mp3, 0)) return make_error(Errc::io_error, "cannot rewind MP3 file: " + path.string());
    std::vector<std::vector<double>> channels(mp3.channels);
    for (auto& c : channels) c.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(total, kMaxReserveFrames)));
    std::vector<float> block(kBlockFrames * mp3.channels);
    for (std::uint64_t done = 0; done < total;) {
        const auto want = std::min<std::uint64_t>(kBlockFrames, total - done);
        const auto got = static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&mp3, want, block.data()));
        for (std::size_t n = 0; n < got; ++n)
            for (std::size_t c = 0; c < channels.size(); ++c) channels[c].push_back(block[n * channels.size() + c]);
        done += got;
        if (got > 0 && !keep_going(progress, done, total)) return make_error(Errc::cancelled, "reading cancelled: " + path.string());
        if (got < want) break;
    }
    if (channels.front().empty()) return make_error(Errc::invalid_argument, "empty MP3: " + path.string());
    WavInfo info{SampleFormat::float32, mp3.channels, mp3.sampleRate, channels.front().size(), false, Container::mp3, 0};
    return WavFile{info, core::AudioBuffer{static_cast<double>(mp3.sampleRate), std::move(channels)}};
}

}  // namespace

core::Result<WavInfo> probe_audio(const std::filesystem::path& path) {
    switch (sniff(path)) {
    case Sniff::missing: return make_error(Errc::io_error, "cannot open file: " + path.string());
    case Sniff::unknown: return std::unexpected{unsupported(path)};
    case Sniff::wav: return probe_wav(path);
    case Sniff::flac: {
        auto* f = open_flac(path);
        if (!f) return make_error(Errc::invalid_argument, "cannot parse FLAC file: " + path.string());
        FlacGuard guard{f};
        const auto frames = flac_frames(*f);
        if (!frames) return make_error(Errc::io_error, "cannot rewind FLAC file: " + path.string());
        return flac_info(*f, *frames);
    }
    case Sniff::mp3: {
        drmp3 mp3;
        if (!open_mp3(mp3, path)) return make_error(Errc::invalid_argument, "cannot parse MP3 file: " + path.string());
        Mp3Guard guard{mp3};
        return WavInfo{SampleFormat::float32, mp3.channels, mp3.sampleRate, drmp3_get_pcm_frame_count(&mp3), false, Container::mp3, 0};
    }
    }
    return std::unexpected{unsupported(path)};
}

core::Result<WavFile> read_audio(const std::filesystem::path& path, core::ProgressSink* progress) {
    switch (sniff(path)) {
    case Sniff::missing: return make_error(Errc::io_error, "cannot open file: " + path.string());
    case Sniff::unknown: return std::unexpected{unsupported(path)};
    case Sniff::wav: return read_wav(path, progress);
    case Sniff::flac: return read_flac(path, progress);
    case Sniff::mp3: return read_mp3(path, progress);
    }
    return std::unexpected{unsupported(path)};
}

}  // namespace signals::io
