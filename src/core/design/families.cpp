#include "core/design/families.hpp"

#include <algorithm>
#include <array>
#include <charconv>

#include "core/design/daubechies.hpp"
#include "core/design/tables.hpp"
#include "core/wavelet/regularity.hpp"

namespace signals::core {
namespace {

constexpr std::array<std::string_view, 106> kNames{
    "bior1.1", "bior1.3", "bior1.5", "bior2.2", "bior2.4", "bior2.6", "bior2.8", "bior3.1", "bior3.3", "bior3.5",
    "bior3.7", "bior3.9", "bior4.4", "bior5.5", "bior6.8", "coif1",   "coif2",   "coif3",   "coif4",   "coif5",
    "coif6",   "coif7",   "coif8",   "coif9",   "coif10",  "coif11",  "coif12",  "coif13",  "coif14",  "coif15",
    "coif16",  "coif17",  "db1",     "db2",     "db3",     "db4",     "db5",     "db6",     "db7",     "db8",
    "db9",     "db10",    "db11",    "db12",    "db13",    "db14",    "db15",    "db16",    "db17",    "db18",
    "db19",    "db20",    "db21",    "db22",    "db23",    "db24",    "db25",    "db26",    "db27",    "db28",
    "db29",    "db30",    "db31",    "db32",    "db33",    "db34",    "db35",    "db36",    "db37",    "db38",
    "dmey",    "haar",    "rbio1.1", "rbio1.3", "rbio1.5", "rbio2.2", "rbio2.4", "rbio2.6", "rbio2.8", "rbio3.1",
    "rbio3.3", "rbio3.5", "rbio3.7", "rbio3.9", "rbio4.4", "rbio5.5", "rbio6.8", "sym2",    "sym3",    "sym4",
    "sym5",    "sym6",    "sym7",    "sym8",    "sym9",    "sym10",   "sym11",   "sym12",   "sym13",   "sym14",
    "sym15",   "sym16",   "sym17",   "sym18",   "sym19",   "sym20"};

struct Parsed {
    std::string_view family;
    int order = 0;
    int nr = 0;
    int nd = 0;
};

std::optional<Parsed> parse(std::string_view name) {
    if (std::ranges::find(kNames, name) == kNames.end()) return std::nullopt;
    if (name == "haar" || name == "dmey") return Parsed{name};
    for (std::string_view family : {"rbio", "bior"}) {
        if (name.starts_with(family)) return Parsed{family, 0, name[family.size()] - '0', name[family.size() + 2] - '0'};
    }
    for (std::string_view family : {"coif", "sym", "db"}) {
        if (!name.starts_with(family)) continue;
        int order = 0;
        std::from_chars(name.data() + family.size(), name.data() + name.size(), order);
        return Parsed{family, order};
    }
    return std::nullopt;
}

Result<FilterBank> from_table(std::string_view name, double pr_tol) {
    const TableEntry* t = find_table(name);
    if (t == nullptr) return make_error(Errc::unknown_wavelet, "no table for " + std::string{name});
    return make_bank<double>(std::string{name}, Laurent{t->dec_lo, 0}, Laurent{t->rec_lo, 0}, pr_tol);
}

bool spline_bior(const Parsed& p) { return p.nr >= 1 && p.nr <= 3; }

Result<FilterBank> bior(const Parsed& p) {
    const std::string name = "bior" + std::to_string(p.nr) + "." + std::to_string(p.nd);
    return spline_bior(p) ? cdf_spline<double>(p.nr, p.nd) : from_table(name, 1e-10);
}

}  // namespace

Result<FilterBank> wavelet(std::string_view name) {
    const auto p = parse(name);
    if (!p) return make_error(Errc::unknown_wavelet, "unknown wavelet: '" + std::string{name} + "'");
    if (p->family == "haar") {
        auto b = daubechies(1);
        if (b) b->name = "haar";
        return b;
    }
    if (p->family == "db") return daubechies(p->order);
    if (p->family == "sym") return symlet(p->order);
    if (p->family == "coif") return from_table(name, 1e-10);
    if (p->family == "dmey") return from_table(name, 5e-3);
    if (p->family == "bior") return bior(*p);
    auto b = bior(*p);
    if (!b) return b;
    return make_bank<double>(std::string{name}, trim(b->f0, 0.0), trim(b->h0, 0.0), spline_bior(*p) ? 1e-12 : 1e-10);
}

std::span<const std::string_view> wavelet_names() { return kNames; }

namespace {

// Above this many zeros at -1 dividing them out of double coefficients is too ill-conditioned to trust.
constexpr int kReliableNumericZeros = 15;

Laurent maxflat_l_squared(int p) {
    const BasicLaurent<Rational> y{{Rational{-1, 4}, Rational{1, 2}, Rational{-1, 4}}, -1};
    BasicLaurent<Rational> b{{Rational{0}}, 0}, y_power{{Rational{1}}, 0};
    Rational binomial{1};
    for (int k = 0; k < p; ++k) {
        if (k > 0) {
            binomial = binomial * (p - 1 + k) / k;
            y_power = multiply(y_power, y);
        }
        b = add(b, scale(y_power, binomial));
    }
    return convert<double>(b);
}

Laurent spline_q_l_squared(int p) {
    auto q = maxflat_halfband<Rational>(p);
    for (int i = 0; i < 2 * p; ++i) q = detail::divide_exactly_by_one_plus_w(q);
    Rational q1{0};
    for (const auto& c : q.coeffs) q1 += c;
    return convert<double>(scale(multiply(q, reverse(q)), Rational{1} / (q1 * q1)));
}

std::optional<double> numeric(const Laurent& h, int zeros) {
    if (zeros > kReliableNumericZeros) return std::nullopt;
    return sobolev_exponent(h, zeros);
}

}  // namespace

Result<Regularity> wavelet_regularity(std::string_view name) {
    const auto p = parse(name);
    if (!p) return make_error(Errc::unknown_wavelet, "unknown wavelet: '" + std::string{name} + "'");
    Regularity r;
    if (p->family == "dmey") return r;
    if (p->family == "haar" || p->family == "db" || p->family == "sym") {
        const int order = p->family == "haar" ? 1 : p->order;
        const double s = sobolev_from_autocorrelation(maxflat_l_squared(order), order);
        return Regularity{order, order, s, s};
    }
    const auto bank = wavelet(name);
    if (!bank) return std::unexpected(bank.error());
    if (p->family == "coif") {
        const int zeros = 2 * p->order;
        return Regularity{zeros, zeros, numeric(bank->h0, zeros), numeric(bank->f0, zeros)};
    }
    const bool swapped = p->family == "rbio";
    const int zeros_h0 = swapped ? p->nr : p->nd;
    const int zeros_f0 = swapped ? p->nd : p->nr;
    if (!spline_bior(*p)) return Regularity{zeros_h0, zeros_f0, numeric(bank->h0, zeros_h0), numeric(bank->f0, zeros_f0)};
    const double spline_side = sobolev_from_autocorrelation(Laurent{{1.0}, 0}, p->nr);
    const double q_side = sobolev_from_autocorrelation(spline_q_l_squared((p->nr + p->nd) / 2), p->nd);
    return swapped ? Regularity{zeros_h0, zeros_f0, spline_side, q_side} : Regularity{zeros_h0, zeros_f0, q_side, spline_side};
}

Result<WaveletInfo> wavelet_info(std::string_view name) {
    const auto p = parse(name);
    if (!p) return make_error(Errc::unknown_wavelet, "unknown wavelet: '" + std::string{name} + "'");
    WaveletInfo info{std::string{name}, std::string{p->family}, false, true, false, std::nullopt};
    const bool biorthogonal_family = p->family == "bior" || p->family == "rbio";
    info.orthogonal = !biorthogonal_family;
    if (p->family == "haar") info.vanishing_moments_psi = 1;
    if (p->family == "db" || p->family == "sym") info.vanishing_moments_psi = p->order;
    if (p->family == "coif") info.vanishing_moments_psi = 2 * p->order;
    if (biorthogonal_family) info.vanishing_moments_psi = p->nr;
    info.generated = p->family == "haar" || p->family == "db" ||
                     (p->family == "sym" && std::ranges::find(kSymletTableFallback, p->order) == kSymletTableFallback.end()) ||
                     (biorthogonal_family && spline_bior(*p));
    return info;
}

}  // namespace signals::core
