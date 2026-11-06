#include "core/spectral.hpp"

#include <cmath>
#include <complex>
#include <numbers>

#include "core/detail/pocketfft.hpp"
#include "core/metrics.hpp"

namespace signals::core {

Result<std::vector<double>> welch_psd(std::span<const double> x, double fs, std::size_t segment, std::size_t overlap,
                                      ProgressSink* progress) {
    if (segment < 2 || overlap >= segment || x.size() < segment || !(fs > 0))
        return make_error(Errc::invalid_argument, "welch: need 2 <= segment <= signal length and overlap < segment");
    std::vector<double> window(segment);
    for (std::size_t n = 0; n < segment; ++n)
        window[n] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(segment));
    double w2 = 0;
    for (double w : window) w2 += w * w;
    const double scale = 1.0 / (fs * w2);
    const std::size_t step = segment - overlap, segments = 1 + (x.size() - segment) / step, bins = segment / 2 + 1;
    std::vector<double> psd(bins, 0.0), buffer(segment);
    std::vector<std::complex<double>> spectrum(bins);
    const pocketfft::shape_t shape{segment};
    const pocketfft::stride_t real_stride{sizeof(double)};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)};
    for (std::size_t s = 0; s < segments; ++s) {
        if (progress && progress->cancelled()) return make_error(Errc::cancelled, "welch: cancelled");
        const auto part = x.subspan(s * step, segment);
        const double mean = neumaier_sum(part) / static_cast<double>(segment);
        for (std::size_t n = 0; n < segment; ++n) buffer[n] = (part[n] - mean) * window[n];
        pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, buffer.data(), spectrum.data(), 1.0);
        for (std::size_t k = 0; k < bins; ++k) psd[k] += std::norm(spectrum[k]) * scale;
        if (progress) progress->report(static_cast<double>(s + 1) / static_cast<double>(segments));
    }
    const std::size_t last = segment % 2 == 0 ? bins - 1 : bins;
    for (std::size_t k = 0; k < bins; ++k) {
        psd[k] /= static_cast<double>(segments);
        if (k > 0 && k < last) psd[k] *= 2;
    }
    return psd;
}

std::vector<double> welch_frequencies(double fs, std::size_t segment) {
    std::vector<double> f(segment / 2 + 1);
    for (std::size_t k = 0; k < f.size(); ++k) f[k] = static_cast<double>(k) * fs / static_cast<double>(segment);
    return f;
}

}  // namespace signals::core
