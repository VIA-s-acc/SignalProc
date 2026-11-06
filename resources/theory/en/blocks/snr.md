+++
id = "snr"
title = "SNR and PSNR"
theory = ["met-snr"]
source = "src/core/metrics.cpp"
function = "snr_db"
test = "tests/core/test_metrics.cpp"
views = ["decomposition"]
+++

## Purpose

Express the error $e = x - \hat x$ in decibels relative to the signal (SNR) or relative to full scale (PSNR).

## Formulas

$$\mathrm{SNR} = 10\lg\frac{\sum_n x[n]^2}{\sum_n e[n]^2}, \qquad \mathrm{PSNR} = 10\lg\frac{p^2}{\mathrm{MSE}}, \qquad \mathrm{MSE} = \frac{1}{N}\sum_n e[n]^2$$

Here $p$ is the peak of the scale: $1$ for a full-scale real signal, $32767$ for int16; it is specified explicitly. An equivalent form is $\mathrm{SNR} = -20\lg\big(\|e\|_2 / \|x\|_2\big)$; a gain of $20\lg 2 \approx 6.02$ dB means half the error amplitude. For denoising one looks at the improvement $\Delta\mathrm{SNR} = \mathrm{SNR}_{\text{after}} - \mathrm{SNR}_{\text{before}}$.

## Algorithm and cost

1. The lengths are compared; if they differ, the result is $-\infty$.
2. $\sum e^2$ is the sum of squared differences with Neumaier compensation, $\sum x^2$ is the energy of the reference signal.
3. The logarithm of the ratio; with zero error (the signals coincide) the result is $+\infty$.

Cost $O(N)$, memory $O(1)$.

## With this block's numbers

The reference signal is the original $x$, the signal under test is $\hat x$.

$\mathrm{SNR} = {{metric.snr_db}}$ relative to the signal.

$\mathrm{PSNR} = {{metric.psnr_db}}$ relative to the peak of the scale.

## Checks

It would be worth checking that the SNR of identical signals is $+\infty$ and that the PSNR matches the formula on known vectors — these checks live in `tests/core/test_metrics.cpp` and `tests/core/test_signal_ops.cpp`; they are not on screen.

## Links

- §14.2 — SNR, PSNR and SegSNR (the last is not part of the block).
- §14.1 — reconstruction error: the same sums of squares without the logarithm.
- §12.1 — denoising, for which the SNR improvement is measured.
- §13.2 — compensated summation.

## Implementation

The ratio of the energy of the reference signal to the energy of the difference, and PSNR via the mean squared error.

```cpp
double snr_db(std::span<const double> reference, std::span<const double> test) {
    if (reference.size() != test.size()) return -std::numeric_limits<double>::infinity();
    const double noise = squared_difference_sum(reference, test);
    if (noise == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(energy(reference) / noise);
}
double psnr_db(std::span<const double> reference, std::span<const double> test, double peak) {
    if (reference.size() != test.size() || reference.empty()) return -std::numeric_limits<double>::infinity();
    const double mse = squared_difference_sum(reference, test) / static_cast<double>(reference.size());
    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(peak * peak / mse);
}
```
