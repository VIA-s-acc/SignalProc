#include "core/conv.hpp"

#include <algorithm>
#include <complex>

#include <pocketfft_hdronly.h>

namespace signals::core {
namespace {

Laurent convolve_fft(const Laurent& a, const Laurent& b) {
    const std::size_t out_len = a.coeffs.size() + b.coeffs.size() - 1;
    const std::size_t n = pocketfft::detail::util::good_size_real(out_len);
    std::vector<double> pa(n, 0.0), pb(n, 0.0);
    std::copy(a.coeffs.begin(), a.coeffs.end(), pa.begin());
    std::copy(b.coeffs.begin(), b.coeffs.end(), pb.begin());
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)};
    std::vector<std::complex<double>> fa(n / 2 + 1), fb(n / 2 + 1);
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, pa.data(), fa.data(), 1.0);
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, pb.data(), fb.data(), 1.0);
    for (std::size_t k = 0; k < fa.size(); ++k) fa[k] *= fb[k];
    pocketfft::c2r(shape, complex_stride, real_stride, 0, pocketfft::BACKWARD, fa.data(), pa.data(),
                   1.0 / static_cast<double>(n));
    pa.resize(out_len);
    return {std::move(pa), a.offset + b.offset};
}

}  // namespace

Laurent convolve(const Laurent& a, const Laurent& b, ConvMethod method) {
    if (a.coeffs.empty() || b.coeffs.empty()) return {{}, a.offset + b.offset};
    if (method == ConvMethod::automatic)
        method = std::min(a.coeffs.size(), b.coeffs.size()) >= fft_threshold ? ConvMethod::fft : ConvMethod::direct;
    return method == ConvMethod::fft ? convolve_fft(a, b) : multiply(a, b);
}

std::vector<double> circular_convolve(std::span<const double> x, const Laurent& h) {
    const auto n = static_cast<std::ptrdiff_t>(x.size());
    std::vector<double> y(x.size(), 0.0);
    if (n == 0) return y;
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) {
        const auto shift = (((h.offset + static_cast<std::ptrdiff_t>(k)) % n) + n) % n;
        for (std::ptrdiff_t m = 0; m < n; ++m)
            y[static_cast<std::size_t>((m + shift) % n)] += h.coeffs[k] * x[static_cast<std::size_t>(m)];
    }
    return y;
}

}  // namespace signals::core
