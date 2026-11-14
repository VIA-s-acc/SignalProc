#include "pipeline/npz_values.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>
#include <utility>

#include "core/extension.hpp"
#include "core/packets.hpp"
#include "io/npy_audio.hpp"

namespace signals::pipeline {
namespace {

using core::Errc;

const std::set<std::string, std::less<>> kTransforms{"dwt", "swt", "wpt", "lift53", "lift53i", "lift97", "dtcwt"};

std::unexpected<core::Error> fail(std::string message) { return core::make_error(Errc::invalid_argument, std::move(message)); }

bool pywt_sign_differs(std::string_view transform) { return transform == "dwt" || transform == "swt"; }

std::vector<std::string> list_keys(std::size_t levels) {
    std::vector<std::string> keys{"cA" + std::to_string(levels)};
    for (std::size_t j = levels; j >= 1; --j) keys.push_back("cD" + std::to_string(j));
    return keys;
}

bool is_path(std::string_view key) { return key == "x" || (!key.empty() && std::all_of(key.begin(), key.end(), [](char ch) { return ch == 'a' || ch == 'd'; })); }

std::optional<std::size_t> list_level(std::string_view key) {
    if (!key.starts_with("cA") || key.size() < 3) return std::nullopt;
    std::size_t v = 0;
    for (const char ch : key.substr(2)) {
        if (ch < '0' || ch > '9') return std::nullopt;
        v = v * 10 + static_cast<std::size_t>(ch - '0');
        if (v > 64) return std::nullopt;
    }
    return v;
}

enum class Part { whole, real, imag };

io::NpArray band_array(const Coeffs& c, std::size_t band, double sign, Part part) {
    std::vector<std::vector<double>> rows;
    for (const auto& ch : c.channels) {
        const auto& v = ch.bands[band];
        std::vector<double> row;
        if (part == Part::whole) {
            row.reserve(v.size());
            for (const double x : v) row.push_back(sign * x);
        } else {
            for (std::size_t i = part == Part::real ? 0 : 1; i < v.size(); i += 2) row.push_back(v[i]);
        }
        rows.push_back(std::move(row));
    }
    if (rows.size() == 1) return io::np_vector(std::move(rows.front()));
    return io::np_matrix(rows);
}

std::optional<std::string> text_of(const io::NpzEntries& e, std::string_view key) {
    const auto* a = io::np_find(e, key);
    if (!a) return std::nullopt;
    const auto* v = std::get_if<std::vector<std::string>>(&a->data);
    if (!v || v->size() != 1) return std::nullopt;
    return v->front();
}

std::optional<double> number_of(const io::NpzEntries& e, std::string_view key) {
    const auto* a = io::np_find(e, key);
    if (!a) return std::nullopt;
    const auto* v = std::get_if<std::vector<double>>(&a->data);
    if (!v || v->size() != 1) return std::nullopt;
    return v->front();
}

struct Rows {
    std::size_t channels = 0, length = 0;
    const std::vector<double>* data = nullptr;
};

core::Result<Rows> rows_of(const io::NpArray& a, std::string_view key) {
    const auto* v = std::get_if<std::vector<double>>(&a.data);
    if (!v) return fail("band '" + std::string{key} + "' is not numeric");
    if (a.shape.size() == 1) return Rows{1, a.shape[0], v};
    if (a.shape.size() == 2) return Rows{a.shape[0], a.shape[1], v};
    return fail("band '" + std::string{key} + "' must be (len,) or (C, len)");
}

}  // namespace

std::vector<std::string> band_keys(const Coeffs& c) {
    if (c.transform == "wpt") {
        std::vector<std::string> keys;
        for (const auto& n : c.basis) {
            const auto path = core::packet_path(n);
            keys.push_back(path.empty() ? std::string{"x"} : path);
        }
        return keys;
    }
    return list_keys(c.levels());
}

core::Result<io::NpzEntries> coeffs_entries(const Coeffs& c) {
    if (c.channels.empty()) return fail("the coefficients have no channels");
    const auto bands = c.channels.front().bands.size();
    for (const auto& ch : c.channels) {
        if (ch.bands.size() != bands) return fail("the channels have different numbers of bands");
        for (std::size_t k = 0; k < bands; ++k)
            if (ch.bands[k].size() != c.channels.front().bands[k].size()) return fail("the channels have bands of different lengths");
    }
    const auto keys = band_keys(c);
    if (keys.size() != bands) return fail("the coefficients do not match their transform '" + c.transform + "'");
    const bool flip = pywt_sign_differs(c.transform);
    io::NpzEntries out;
    for (std::size_t k = 0; k < bands; ++k) {
        if (c.transform == "dtcwt" && k > 0) {
            out.emplace_back("re_" + keys[k], band_array(c, k, 1, Part::real));
            out.emplace_back("im_" + keys[k], band_array(c, k, 1, Part::imag));
        } else {
            out.emplace_back(keys[k], band_array(c, k, flip && k > 0 ? -1.0 : 1.0, Part::whole));
        }
    }
    const auto length = !c.lengths.empty() ? c.lengths.front() : c.channels.front().original_length;
    out.emplace_back("wavelet", io::np_text(c.wavelet));
    out.emplace_back("mode", io::np_text(std::string{core::to_string(c.mode)}));
    out.emplace_back("rate", io::np_scalar(c.rate));
    out.emplace_back("transform", io::np_text(c.transform));
    out.emplace_back("length", io::np_scalar(static_cast<double>(length)));
    out.emplace_back("detail_sign", io::np_text(flip || c.transform == "wpt" ? "pywt" : "signals"));
    if (c.transform == "swt") out.emplace_back("norm", io::np_scalar(c.norm ? 1 : 0));
    return out;
}

io::NpzEntries report_entries(const Report& r) {
    io::NpzEntries out;
    std::vector<std::string> names;
    for (const auto& [name, value] : r.fields) {
        std::visit(
            [&, &key = name](const auto& x) {
                if constexpr (std::is_same_v<std::decay_t<decltype(x)>, double>) out.emplace_back(key, io::np_scalar(x));
                else out.emplace_back(key, io::np_text(x));
            },
            value);
        names.push_back(name);
    }
    out.emplace_back("passed", io::np_scalar(r.passed ? 1 : 0));
    const auto n = names.size();
    out.emplace_back("fields", io::NpArray{{n}, std::move(names)});
    return out;
}

io::NpzEntries bank_entries(const core::FilterBank& b) {
    const auto negated = [](std::vector<double> v) {
        for (auto& x : v) x = -x;
        return v;
    };
    return {{"dec_lo", io::np_vector(b.h0.coeffs)},
            {"dec_hi", io::np_vector(negated(b.h1.coeffs))},
            {"rec_lo", io::np_vector(b.f0.coeffs)},
            {"rec_hi", io::np_vector(negated(b.f1.coeffs))},
            {"name", io::np_text(b.name)}};
}

core::Result<io::NpzEntries> value_entries(const Value& v) {
    if (const auto* a = std::get_if<core::AudioBuffer>(&v)) return io::signal_entries(*a);
    if (const auto* c = std::get_if<Coeffs>(&v)) return coeffs_entries(*c);
    if (const auto* r = std::get_if<Report>(&v)) return report_entries(*r);
    return fail("a " + std::string{to_string(type_of(v))} + " value cannot be written to .npz");
}

io::NpzEntries prefixed(std::string_view prefix, io::NpzEntries entries) {
    for (auto& [key, a] : entries) key = std::string{prefix} + "." + key;
    return entries;
}

core::Result<Coeffs> coeffs_from_entries(const io::NpzEntries& e, const CoeffsHints& hints) {
    bool has_list = false, has_path = false;
    std::optional<std::size_t> levels;
    for (const auto& [key, a] : e) {
        if (const auto j = list_level(key)) has_list = true, levels = j;
        if (is_path(key)) has_path = true;
    }
    auto transform = text_of(e, "transform");
    if (!transform) {
        if (has_list) transform = "dwt";
        else if (has_path) transform = "wpt";
        else return fail("the file holds no coefficients: expected cA/cD keys or wavelet packet paths");
    }
    if (!kTransforms.contains(*transform)) return fail("unknown transform '" + *transform + "'");
    Coeffs c;
    c.transform = *transform;
    auto wavelet = text_of(e, "wavelet");
    if (!wavelet && !hints.wavelet.empty()) wavelet = hints.wavelet;
    if (!wavelet) return fail("the file has no 'wavelet'; set the wavelet");
    c.wavelet = *wavelet;
    auto mode = text_of(e, "mode");
    if (!mode && !hints.mode.empty()) mode = hints.mode;
    if (!mode) {
        if (c.transform == "dwt" || c.transform == "wpt") return fail("the file has no 'mode'; set the mode");
        mode = c.transform == "swt" ? "periodization" : "symmetric";
    }
    const auto ext = core::parse_extension(*mode);
    if (!ext) return fail("unknown mode '" + *mode + "'");
    c.mode = *ext;
    c.rate = number_of(e, "rate").value_or(hints.rate.value_or(0));
    const auto sign = text_of(e, "detail_sign");
    if (sign && *sign != "pywt" && *sign != "signals") return fail("detail_sign must be 'pywt' or 'signals', not '" + *sign + "'");
    const bool to_signals = pywt_sign_differs(c.transform) && (!sign || *sign == "pywt");
    c.norm = number_of(e, "norm").value_or(0) != 0;

    std::vector<std::string> keys;
    if (c.transform == "wpt") {
        std::size_t deepest = 0;
        for (const auto& [key, a] : e)
            if (is_path(key)) {
                keys.push_back(key);
                c.basis.push_back(key == "x" ? core::PacketNode{0, 0} : core::PacketNode{key.size(), 0});
                for (const char ch : key == "x" ? std::string{} : key) c.basis.back().index = c.basis.back().index * 2 + (ch == 'd' ? 1 : 0);
                deepest = std::max(deepest, c.basis.back().level);
            }
        if (keys.empty()) return fail("the file holds no wavelet packet nodes");
        if (!core::admissible(c.basis, deepest)) return fail("the wavelet packet nodes in the file do not form a basis");
    } else {
        if (!levels) return fail("the file has no cA band");
        keys = list_keys(*levels);
    }

    std::size_t channels = 0;
    std::vector<std::vector<Rows>> parts;
    for (std::size_t k = 0; k < keys.size(); ++k) {
        std::vector<std::string> names{keys[k]};
        if (c.transform == "dtcwt" && k > 0) names = {"re_" + keys[k], "im_" + keys[k]};
        std::vector<Rows> rows;
        for (const auto& name : names) {
            const auto* a = io::np_find(e, name);
            if (!a) return fail("the file misses band '" + name + "'");
            auto r = rows_of(*a, name);
            if (!r) return std::unexpected{r.error()};
            if (channels == 0) channels = r->channels;
            if (r->channels != channels) return fail("band '" + name + "' has a different number of channels");
            rows.push_back(*r);
        }
        if (rows.size() == 2 && rows[0].length != rows[1].length) return fail("the real and imaginary parts of '" + keys[k] + "' differ in length");
        parts.push_back(std::move(rows));
    }
    if (channels == 0) return fail("the coefficients have no channels");
    std::size_t bound = 0;
    for (const auto& r : parts)
        for (const auto& part : r) bound += part.length;
    std::size_t length = 0;
    const auto given = number_of(e, "length");
    if (!given && c.transform == "wpt") return fail("a wavelet packet file needs 'length', the length of the original signal");
    if (given) {
        if (!std::isfinite(*given) || *given < 0 || std::floor(*given) != *given || *given > static_cast<double>(bound))
            return fail("length " + std::to_string(*given) + " does not fit the bands: expected a whole number up to " + std::to_string(bound));
        length = static_cast<std::size_t>(*given);
    }
    for (std::size_t ch = 0; ch < channels; ++ch) {
        core::BasicCoeffs<double> b;
        b.original_length = length;
        for (std::size_t k = 0; k < parts.size(); ++k) {
            const auto& r = parts[k];
            std::vector<double> band;
            if (r.size() == 2) {
                band.reserve(2 * r[0].length);
                for (std::size_t i = 0; i < r[0].length; ++i) band.push_back((*r[0].data)[ch * r[0].length + i]), band.push_back((*r[1].data)[ch * r[1].length + i]);
            } else {
                const double s = to_signals && k > 0 ? -1.0 : 1.0;
                band.reserve(r[0].length);
                for (std::size_t i = 0; i < r[0].length; ++i) band.push_back(s * (*r[0].data)[ch * r[0].length + i]);
            }
            b.bands.push_back(std::move(band));
        }
        c.channels.push_back(std::move(b));
        if (given) c.lengths.push_back(length);
    }
    return c;
}

}  // namespace signals::pipeline
