+++
id = "gain"
title = "Gain in dB"
theory = ["conv-signals", "freq-magnitude"]
source = "src/core/signal_ops.cpp"
function = "gain_db"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Multiply the signal by a constant factor given in decibels: raise or lower the level without changing the shape of the signal or its spectrum.

## Formulas

$$y[n] = g\, x[n], \qquad g = 10^{G/20}, \qquad G = 20 \log_{10} g$$

The factor is 20 (not 10) because decibels measure power, and power is proportional to the square of the amplitude: $10 \log_{10} g^2 = 20 \log_{10} g$. Hence $+6.02$ dB is $g = 2$, $-6.02$ dB is $g = \tfrac12$, and $+20$ dB is $g = 10$.

The operation is linear and time-invariant: $Y(z) = g\, X(z)$; the shape of the input's magnitude response and the phase response do not change. The peak and RMS in dBFS shift by exactly $G$, and the signal-to-noise ratio does not change:

$$P_y = P_x + G, \qquad \mathrm{SNR}_y = \mathrm{SNR}_x$$

## Algorithm and cost

1. $g = 10^{G/20}$ is computed once.
2. A copy of the input is multiplied by $g$ element-wise.
3. No clipping is done: if $\max \lvert y \rvert \ge 1$, exceeding full scale is handled by WAV writing (saturation and a clip counter).

Time $O(N)$, memory $O(N)$ (a separate output vector). One multiplication in double is one rounding with a relative error of at most $2^{-53}$.

## With this block's numbers

The gain $G = {{block.gain_db}}$ dB gives the factor $g = 10^{ {{block.gain_db}} / 20}$: each sample is multiplied by it, and the peak and RMS shift by $G$ dB. If $G > 0$, the margin to full scale (headroom) shrinks by the same $G$ dB: a peak above $-G$ dBFS will leave $[-1, 1)$ after the gain.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The test compares the result with an exact doubling: with $G = 20 \log_{10} 2$ the signal $(1, -0.5)$ becomes $(2, -1)$ with no error.

## Links

- §0.1 — the signal $x[n]$ and the $z$-transform: multiplication by a constant is linear.
- §8.2 — decibels and $20 \lg$ for amplitude (the logarithmic magnitude response is built the same way).
- The "Level normalization" block picks $G$ automatically from the peak or RMS.
- The "Write WAV" block handles exceeding full scale.

## Implementation

`gain_db` is `scaled(x, db_to_amplitude(db))`: conversion of decibels to a factor and multiplication of a copy of the signal.

```cpp
std::vector<double> scaled(std::span<const double> x, double factor) {
    std::vector<double> y(x.begin(), x.end());
    for (double& v : y) v *= factor;
    return y;
}

double db_to_amplitude(double db) { return std::pow(10.0, db / 20.0); }
```
