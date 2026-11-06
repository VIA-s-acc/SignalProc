+++
id = "normalize"
title = "Level normalization"
theory = ["conv-signals", "freq-magnitude"]
source = "src/core/signal_ops.cpp"
function = "normalize_peak"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Bring the signal level to a given value in dBFS: either so that the maximum magnitude equals the target (by peak), or so that the root-mean-square value equals the target (by RMS). The shape of the signal does not change.

## Formulas

The target $T$ in dBFS is converted to the amplitude $10^{T/20}$. By peak, $P = \max_n \lvert x[n] \rvert$:

$$y[n] = \frac{10^{T/20}}{P}\, x[n], \qquad \max_n \lvert y[n] \rvert = 10^{T/20}$$

By RMS, $\rho = \sqrt{\tfrac{1}{N} \sum_{n=0}^{N-1} x[n]^2}$:

$$y[n] = \frac{10^{T/20}}{\rho}\, x[n], \qquad \sqrt{\tfrac{1}{N} \sum_n y[n]^2} = 10^{T/20}$$

This is a gain $y = g\,x$ with $G = T - L_x$ dB, where $L_x = 20 \log_{10} P$ or $20 \log_{10} \rho$ is the current level. If $P = 0$ or $\rho = 0$ (silence), the signal is left as is.

Normalization by RMS does not guarantee the absence of clipping: the peak of the output signal is $T + C$ dBFS, where $C = 20 \log_{10}(P / \rho)$ is the crest factor. Clipping starts as soon as $T + C \ge 0$.

## Algorithm and cost

1. One pass: the maximum magnitude (by peak) or the sum of squares with Neumaier compensated summation (by RMS).
2. If the value found is zero, a copy of the input is returned.
3. Otherwise the factor $10^{T/20}$ divided by the level found is applied to all samples.

Two passes over $N$ samples: finding the level and scaling; memory $O(N)$. After scaling the level matches the target up to rounding error.

## With this block's numbers

Mode {{block.kind}}, target $T = {{block.target_dbfs}}$ dBFS, i.e. the amplitude $10^{ {{block.target_dbfs}} / 20}$ as a fraction of full scale. The peak mode sets the maximum magnitude to this value; the RMS mode sets the root-mean-square value, and then the peaks end up above the target by the crest factor.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The tests check that after peak normalization the maximum equals the target, that after RMS normalization the RMS equals the target (both checks to within $10^{-12}$ dB), and that a zero signal stays zero.

## Links

- §0.1 — the signal $x[n]$; normalization is multiplication by a constant.
- §8.2 — decibels: $20 \lg$ for amplitude.
- §13.2 — Neumaier summation for the energy of long signals.
- The "Gain in dB" block — the same operation with a given $G$ instead of a computed one.

## Implementation

Two normalization branches: the maximum magnitude or the RMS via the energy, then a single scaling.

```cpp
std::vector<double> normalize_peak(std::span<const double> x, double target_dbfs) {
    double peak = 0;
    for (double v : x) peak = std::max(peak, std::abs(v));
    if (peak == 0) return {x.begin(), x.end()};
    return scaled(x, db_to_amplitude(target_dbfs) / peak);
}

std::vector<double> normalize_rms(std::span<const double> x, double target_dbfs) {
    const double rms = x.empty() ? 0.0 : std::sqrt(energy(x) / static_cast<double>(x.size()));
    if (rms == 0) return {x.begin(), x.end()};
    return scaled(x, db_to_amplitude(target_dbfs) / rms);
}
```
