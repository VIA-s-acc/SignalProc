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

enum class Kind { npy, npz, other };

Kind kind_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    char b[6] = {};
    in.read(b, sizeof b);
    if (in.gcount() >= 6 && std::string_view{b, 6} == std::string_view{"\x93NUMPY", 6}) return Kind::npy;
    if (in.gcount() >= 4 && b[0] == 'P' && b[1] == 'K' && (b[2] == '\x03' || b[2] == '\x05')) return Kind::npz;
    return Kind::other;
}

struct Layout {
    std::size_t channels = 0, frames = 0;
};

core::Result<Layout> layout_of(const std::vector<std::size_t>& shape, bool text) {
    if (text) return fail("an array of strings is not a signal");
    if (shape.size() == 1) return Layout{1, shape[0]};
    if (shape.size() == 2) {
        if (shape[0] > kMaxChannels)
            return fail("an array of shape " + shape_text(shape) + " does not look like (C, N): it has more than " + std::to_string(kMaxChannels) +
                        " rows; for (N, C) data save x.T");
        return Layout{shape[0], shape[1]};
    }
    return fail("an array of shape " + shape_text(shape) + " is not a signal: expected (N,) or (C, N)");
}

using Members = std::vector<std::pair<std::string, NpHeader>>;

std::string member_list(const Members& m) {
    std::string out;
    for (const auto& [k, h] : m) out += (out.empty() ? "" : ", ") + k;
    return out;
}

core::Result<std::pair<std::string, NpHeader>> pick(const Members& m, const std::string& key) {
    const auto find = [&](std::string_view k) -> const std::pair<std::string, NpHeader>* {
        for (const auto& e : m)
            if (e.first == k) return &e;
        return nullptr;
    };
    if (!key.empty()) {
        if (const auto* e = find(key)) return *e;
        return fail("no array '" + key + "' in the file; keys: " + member_list(m));
    }
    if (const auto* e = find("x")) return *e;
    const std::pair<std::string, NpHeader>* only = nullptr;
    std::size_t count = 0;
    for (const auto& e : m)
        if (e.first != "rate") only = &e, ++count;
    if (count == 1) return *only;
    return fail("the file holds several arrays (" + member_list(m) + "); choose one with key");
}

core::Result<std::optional<double>> file_rate(const std::filesystem::path& path, const Members& m) {
    bool has = false;
    for (const auto& e : m) has = has || e.first == "rate";
    if (!has) return std::optional<double>{};
    const auto entries = read_npz(path, kAudioLimits, {"rate"});
    if (!entries) return std::unexpected{entries.error()};
    const auto* r = np_find(*entries, "rate");
    const auto* v = r ? std::get_if<std::vector<double>>(&r->data) : nullptr;
    if (!v || v->size() != 1) return fail("'rate' in the file is not a single number");
    return std::optional<double>{v->front()};
}

WavInfo info_of(const Layout& l, double rate, Container container) {
    WavInfo info;
    info.format = SampleFormat::float64;
    info.channels = static_cast<unsigned>(l.channels);
    info.sample_rate = static_cast<unsigned>(std::lround(rate));
    info.frames = l.frames;
    info.container = container;
    info.bits = 64;
    return info;
}

core::Result<WavFile> as_file(const NpArray& a, double rate, Container container) {
    auto audio = audio_from_array(a, rate);
    if (!audio) return std::unexpected{audio.error()};
    const auto info = info_of({audio->channels.size(), audio->frames()}, rate, container);
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
    const auto l = layout_of(a.shape, v == nullptr);
    if (!l) return std::unexpected{l.error()};
    if (!(rate > 0) || !std::isfinite(rate)) return fail("the sample rate must be a positive number, got " + number(rate));
    core::AudioBuffer out{rate, {}};
    for (std::size_t c = 0; c < l->channels; ++c)
        out.channels.emplace_back(v->begin() + static_cast<std::ptrdiff_t>(c * l->frames), v->begin() + static_cast<std::ptrdiff_t>((c + 1) * l->frames));
    return out;
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
        const auto members = probe_npz(path, kAudioLimits);
        if (!members) return std::unexpected{members.error()};
        const auto picked = pick(*members, options.key);
        if (!picked) return at(path, picked.error());
        const auto in_file = file_rate(path, *members);
        if (!in_file) return at(path, in_file.error());
        if (*in_file && options.rate && std::abs(**in_file - *options.rate) > 1e-9 * std::abs(*options.rate))
            return at(path, {Errc::invalid_argument, "the sample rate in the file (" + number(**in_file) + ") contradicts the given rate (" +
                                                         number(*options.rate) + ")"});
        const auto rate = options.rate ? options.rate : *in_file;
        if (!rate) return at(path, {Errc::invalid_argument, "the file has no 'rate' key; set the rate"});
        const auto entries = read_npz(path, kAudioLimits, {picked->first});
        if (!entries) return std::unexpected{entries.error()};
        const auto* array = np_find(*entries, picked->first);
        if (!array) return at(path, {Errc::invalid_argument, "array '" + picked->first + "' disappeared while reading; the file changed"});
        auto f = as_file(*array, *rate, Container::npz);
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
        const auto h = probe_npy(path, kAudioLimits);
        if (!h) return std::unexpected{h.error()};
        const auto l = layout_of(h->shape, h->text);
        if (!l) return at(path, l.error());
        auto info = info_of(*l, 1, Container::npy);
        info.sample_rate = 0;
        return info;
    }
    if (k == Kind::npz) {
        const auto members = probe_npz(path, kAudioLimits);
        if (!members) return std::unexpected{members.error()};
        const auto picked = pick(*members, "");
        if (!picked) return at(path, picked.error());
        const auto l = layout_of(picked->second.shape, picked->second.text);
        if (!l) return at(path, l.error());
        const auto rate = file_rate(path, *members);
        if (!rate) return at(path, rate.error());
        auto info = info_of(*l, rate->value_or(1), Container::npz);
        if (!*rate) info.sample_rate = 0;
        return info;
    }
    return at(path, {Errc::invalid_argument, "not an .npy or .npz file"});
}

}  // namespace signals::io
