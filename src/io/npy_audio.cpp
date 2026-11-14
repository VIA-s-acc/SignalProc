#include "io/npy_audio.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace signals::io {
namespace {

using core::Errc;

constexpr std::size_t kMaxChannels = 64;
constexpr NpLimits kAudioLimits{std::size_t{1} << 28, std::size_t{1} << 28};

std::unexpected<core::Error> fail(std::string message) { return core::make_error(Errc::invalid_argument, std::move(message)); }

std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return {u.begin(), u.end()};
}

std::string number(double v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

std::string shape_text(const std::vector<std::size_t>& shape) {
    std::string out = "(";
    for (std::size_t k = 0; k < shape.size(); ++k) out += (k ? ", " : "") + std::to_string(shape[k]);
    if (shape.size() == 1) out += ",";
    return out + ")";
}

std::string key_list(const NpzEntries& e) {
    std::string out;
    for (const auto& [k, a] : e) out += (out.empty() ? "" : ", ") + k;
    return out;
}

enum class Kind { npy, npz, other };

Kind kind_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    char b[6] = {};
    in.read(b, sizeof b);
    if (in.gcount() >= 6 && std::string_view{b, 6} == std::string_view{"\x93NUMPY", 6}) return Kind::npy;
    if (in.gcount() >= 4 && b[0] == 'P' && b[1] == 'K' && (b[2] == '\x03' || b[2] == '\x05')) return Kind::npz;
    return Kind::other;
}

struct Picked {
    const NpArray* array = nullptr;
    std::optional<double> rate;
};

core::Result<Picked> pick(const NpzEntries& e, const std::string& key) {
    Picked p;
    if (const auto* r = np_find(e, "rate")) {
        const auto* v = std::get_if<std::vector<double>>(&r->data);
        if (!v || v->size() != 1) return fail("'rate' in the file is not a single number");
        p.rate = v->front();
    }
    if (!key.empty()) {
        p.array = np_find(e, key);
        if (!p.array) return fail("no array '" + key + "' in the file; keys: " + key_list(e));
        return p;
    }
    if ((p.array = np_find(e, "x"))) return p;
    std::vector<const NpArray*> candidates;
    for (const auto& [k, a] : e)
        if (k != "rate") candidates.push_back(&a);
    if (candidates.size() == 1) {
        p.array = candidates.front();
        return p;
    }
    return fail("the file holds several arrays (" + key_list(e) + "); choose one with key");
}

core::Result<WavFile> as_file(const NpArray& a, double rate, Container container) {
    auto audio = audio_from_array(a, rate);
    if (!audio) return std::unexpected{audio.error()};
    WavInfo info;
    info.format = SampleFormat::float64;
    info.channels = static_cast<unsigned>(audio->channels.size());
    info.sample_rate = static_cast<unsigned>(std::lround(rate));
    info.frames = audio->frames();
    info.container = container;
    info.bits = 64;
    return WavFile{info, std::move(*audio)};
}

std::unexpected<core::Error> at(const std::filesystem::path& p, const core::Error& e) {
    if (e.message.starts_with(utf8(p))) return std::unexpected{e};
    return core::make_error(e.code, utf8(p) + ": " + e.message);
}

}  // namespace

NpzEntries signal_entries(const core::AudioBuffer& a) {
    NpArray x = a.channels.size() == 1 ? np_vector(a.channels.front()) : np_matrix(a.channels);
    return {{"x", std::move(x)}, {"rate", np_scalar(a.sample_rate)}};
}

core::Result<core::AudioBuffer> audio_from_array(const NpArray& a, double rate) {
    const auto* v = std::get_if<std::vector<double>>(&a.data);
    if (!v) return fail("an array of strings is not a signal");
    if (!(rate > 0) || !std::isfinite(rate)) return fail("the sample rate must be a positive number, got " + number(rate));
    if (a.shape.size() == 1) return core::AudioBuffer{rate, {*v}};
    if (a.shape.size() == 2) {
        const auto rows = a.shape[0], cols = a.shape[1];
        if (rows > kMaxChannels)
            return fail("an array of shape " + shape_text(a.shape) + " does not look like (C, N): it has more than " + std::to_string(kMaxChannels) +
                        " rows; for (N, C) data save x.T");
        core::AudioBuffer out{rate, {}};
        for (std::size_t c = 0; c < rows; ++c)
            out.channels.emplace_back(v->begin() + static_cast<std::ptrdiff_t>(c * cols), v->begin() + static_cast<std::ptrdiff_t>((c + 1) * cols));
        return out;
    }
    return fail("an array of shape " + shape_text(a.shape) + " is not a signal: expected (N,) or (C, N)");
}

core::Result<WavFile> read_array_audio(const std::filesystem::path& path, const ArrayAudioOptions& options) {
    switch (kind_of(path)) {
    case Kind::npy: {
        const auto a = read_npy(path, kAudioLimits);
        if (!a) return std::unexpected{a.error()};
        if (!options.rate) return at(path, {Errc::invalid_argument, "an .npy file has no sample rate; set the rate"});
        auto f = as_file(*a, *options.rate, Container::npy);
        if (!f) return at(path, f.error());
        return f;
    }
    case Kind::npz: {
        const auto entries = read_npz(path, kAudioLimits);
        if (!entries) return std::unexpected{entries.error()};
        const auto picked = pick(*entries, options.key);
        if (!picked) return at(path, picked.error());
        if (picked->rate && options.rate && std::abs(*picked->rate - *options.rate) > 1e-9 * std::abs(*options.rate))
            return at(path, {Errc::invalid_argument, "the sample rate in the file (" + number(*picked->rate) + ") contradicts the given rate (" +
                                                         number(*options.rate) + ")"});
        const auto rate = options.rate ? options.rate : picked->rate;
        if (!rate) return at(path, {Errc::invalid_argument, "the file has no 'rate' key; set the rate"});
        auto f = as_file(*picked->array, *rate, Container::npz);
        if (!f) return at(path, f.error());
        return f;
    }
    case Kind::other: break;
    }
    return at(path, {Errc::invalid_argument, "not an .npy or .npz file"});
}

core::Result<WavInfo> probe_array_audio(const std::filesystem::path& path) {
    const auto k = kind_of(path);
    if (k == Kind::npy) {
        const auto a = read_npy(path, kAudioLimits);
        if (!a) return std::unexpected{a.error()};
        auto f = as_file(*a, 1, Container::npy);
        if (!f) return at(path, f.error());
        f->info.sample_rate = 0;
        return f->info;
    }
    if (k == Kind::npz) {
        const auto entries = read_npz(path, kAudioLimits);
        if (!entries) return std::unexpected{entries.error()};
        const auto picked = pick(*entries, "");
        if (!picked) return at(path, picked.error());
        auto f = as_file(*picked->array, picked->rate.value_or(1), Container::npz);
        if (!f) return at(path, f.error());
        if (!picked->rate) f->info.sample_rate = 0;
        return f->info;
    }
    return at(path, {Errc::invalid_argument, "not an .npy or .npz file"});
}

}  // namespace signals::io
