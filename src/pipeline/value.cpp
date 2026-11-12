#include "pipeline/value.hpp"

#include <cstring>

namespace signals::pipeline {
namespace {

struct Hasher {
    std::uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
    void number(double v) { bytes(&v, sizeof v); }
    void count(std::size_t n) { bytes(&n, sizeof n); }
    void text(const std::string& s) {
        count(s.size());
        bytes(s.data(), s.size());
    }
    void doubles(const std::vector<double>& v) {
        count(v.size());
        if (!v.empty()) bytes(v.data(), v.size() * sizeof(double));
    }
};

}  // namespace

PortType type_of(const Value& v) { return static_cast<PortType>(v.index()); }

std::string_view to_string(PortType t) {
    switch (t) {
    case PortType::signal: return "signal";
    case PortType::coeffs: return "coeffs";
    case PortType::filter: return "filter";
    case PortType::scalar: return "scalar";
    case PortType::report: return "report";
    }
    return "?";
}

OutputShape shape_of(const Value& v) {
    OutputShape s{type_of(v)};
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) {
        s.channels = a->channels.size();
        s.frames = a->frames();
        s.rate = a->sample_rate;
    } else if (const auto* c = std::get_if<Coeffs>(&v)) {
        s.channels = c->channels.size();
        if (!c->channels.empty())
            for (const auto& band : c->channels.front().bands) s.frames += band.size();
        s.rate = c->rate;
    }
    return s;
}

std::uint64_t value_key(const Value& v) {
    Hasher h;
    h.count(v.index());
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, core::AudioBuffer>) {
                h.number(x.sample_rate);
                for (const auto& c : x.channels) h.doubles(c);
            } else if constexpr (std::is_same_v<T, Coeffs>) {
                h.text(x.wavelet);
                h.text(x.transform);
                h.count(x.norm ? 1 : 0);
                h.count(static_cast<std::size_t>(x.mode));
                h.number(x.rate);
                for (const auto& n : x.basis) {
                    h.count(n.level);
                    h.count(n.index);
                }
                for (const auto& c : x.channels)
                    for (const auto& b : c.bands) h.doubles(b);
            } else if constexpr (std::is_same_v<T, core::FilterBank>) {
                for (const auto* f : {&x.h0, &x.h1, &x.f0, &x.f1}) {
                    h.count(static_cast<std::size_t>(f->offset));
                    h.doubles(f->coeffs);
                }
            } else if constexpr (std::is_same_v<T, double>) {
                h.number(x);
            } else {
                for (const auto& [name, value] : x.fields) {
                    h.text(name);
                    if (const auto* d = std::get_if<double>(&value)) h.number(*d);
                    else h.text(std::get<std::string>(value));
                }
            }
        },
        v);
    return h.h;
}

std::size_t value_bytes(const Value& v) {
    return std::visit(
        [](const auto& x) -> std::size_t {
            using T = std::decay_t<decltype(x)>;
            std::size_t n = 0;
            if constexpr (std::is_same_v<T, core::AudioBuffer>) {
                for (const auto& c : x.channels) n += c.size() * sizeof(double);
            } else if constexpr (std::is_same_v<T, Coeffs>) {
                for (const auto& c : x.channels)
                    for (const auto& b : c.bands) n += b.size() * sizeof(double);
            } else if constexpr (std::is_same_v<T, core::FilterBank>) {
                n = (x.h0.coeffs.size() + x.h1.coeffs.size() + x.f0.coeffs.size() + x.f1.coeffs.size()) * sizeof(double);
            } else {
                n = sizeof(T);
            }
            return n;
        },
        v);
}

std::optional<std::size_t> band_index(const Coeffs& c, std::string_view name) {
    for (std::size_t k = 0; k < c.basis.size(); ++k)
        if (core::packet_path(c.basis[k]) == name) return k;
    const auto levels = c.levels();
    if (name == "a" || name == "a" + std::to_string(levels)) return 0;
    if (name.size() < 2 || name[0] != 'd') return std::nullopt;
    std::size_t k = 0;
    for (char ch : name.substr(1)) {
        if (ch < '0' || ch > '9') return std::nullopt;
        k = k * 10 + static_cast<std::size_t>(ch - '0');
    }
    if (k < 1 || k > levels) return std::nullopt;
    return levels + 1 - k;
}

std::optional<Value> band_port(const Coeffs& c, std::string_view name) {
    const auto i = band_index(c, name);
    if (!i) return std::nullopt;
    const auto level = *i < c.basis.size() ? c.basis[*i].level : *i == 0 ? c.levels() : c.levels() + 1 - *i;
    core::AudioBuffer out;
    out.sample_rate = c.transform != "swt" ? c.rate / static_cast<double>(std::size_t{1} << level) : c.rate;
    for (const auto& ch : c.channels) out.channels.push_back(ch.bands[*i]);
    return Value{std::move(out)};
}

std::string band_label(const Coeffs& c, std::size_t band) {
    if (band < c.basis.size()) {
        auto path = core::packet_path(c.basis[band]);
        return path.empty() ? std::string{"x"} : path;
    }
    const auto levels = c.levels();
    return band == 0 ? "a" + std::to_string(levels) : "d" + std::to_string(levels + 1 - band);
}

}  // namespace signals::pipeline
