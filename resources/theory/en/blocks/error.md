+++
id = "error"
title = "Reconstruction error"
theory = ["met-reconstruction"]
source = "src/core/metrics.cpp"
function = "max_abs_error"
test = "tests/core/test_metrics.cpp"
views = ["decomposition"]
+++

## Purpose

Measure how much the reconstructed signal $\hat x$ differs from the original $x$ with three numbers: the worst-case, the mean and the relative error.

## Formulas

$$e = x - \hat x, \qquad \max\lvert e\rvert = \max_n \lvert e[n]\rvert, \qquad \mathrm{RMS} = \sqrt{\overline{e^2}} = \sqrt{\frac{1}{N}\sum_n e[n]^2}, \qquad \frac{\|e\|_2}{\|x\|_2}$$

Tolerance for perfect reconstruction with $J$ levels and a filter of length $L$ (§13.2), $u = 2^{-53}$ for double:

$$\varepsilon = 100\, J\, L\, u\,\|x\|_\infty$$

## Algorithm and cost

1. The lengths of $x$ and $\hat x$ are compared; if they differ, the error is infinite.
2. $\max\lvert e\rvert$ — one pass that remembers the largest $\lvert e[n]\rvert$; a NaN in the difference is returned as NaN so that the check fails.
3. $\mathrm{RMS}$ and the relative error — sums of squares with Neumaier compensation (§13.2), then a square root.

Cost $O(N)$, memory $O(1)$.

## With this block's numbers

Worst-case error $\max\lvert e\rvert = {{metric.max_abs}}$, root-mean-square $\mathrm{RMS} = {{metric.rms}}$, relative $\|e\|_2 / \|x\|_2 = {{metric.relative}}$.

## Checks

- Perfect reconstruction on this signal, $\max\lvert \hat x - x\rvert \le \varepsilon$: {{check.round_trip}}
- A NaN in either signal never passes the check.

## Links

- §14.1 — definitions of $\max\lvert e\rvert$, RMS and the relative error.
- §13.2 — error growth across levels and the test tolerance.
- §14.2 — SNR and PSNR: the same sums of squares in decibels.
- §11.1 — the DWT and the inverse transform whose reconstruction is checked.
- §1.1 — the error of FFT convolution is absolute, so compare against $\|x\|$, not element-wise.

## Implementation

Worst-case and root-mean-square error of two signals.

```cpp
double max_abs_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = std::abs(a[i] - b[i]);
        if (std::isnan(d)) return d;
        if (d > worst) worst = d;
    }
    return worst;
}
double rms_error(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size()) return std::numeric_limits<double>::infinity();
    if (a.empty()) return 0.0;
    return std::sqrt(squared_difference_sum(a, b) / static_cast<double>(a.size()));
}
```
