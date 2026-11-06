+++
id = "remove_dc"
title = "Remove DC offset"
theory = ["conv-signals", "lti-z-dtft"]
source = "src/core/signal_ops.cpp"
function = "remove_dc"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Remove a constant offset (DC offset) from the signal: subtract the mean so that the signal oscillates around zero. This frees amplitude headroom and removes the peak at zero frequency in the spectrum.

## Formulas

$$\bar x = \frac{1}{N} \sum_{n=0}^{N-1} x[n], \qquad y[n] = x[n] - \bar x$$

The zeroth DFT harmonic is $X[0] = \sum_n x[n] = N \bar x$, so after the subtraction

$$\sum_{n=0}^{N-1} y[n] = 0, \qquad Y[0] = 0, \qquad Y[k] = X[k] \;\; (k \ne 0)$$

The other harmonics do not change, and the energy decreases by exactly the energy of the offset:

$$\sum_n y[n]^2 = \sum_n x[n]^2 - N \bar x^2$$

This is not a highpass filter: a single constant is subtracted from the whole signal, so the phase and amplitude of the other frequencies are not distorted, but a slow drift of the mean (an offset that changes over time) is not removed.

## Algorithm and cost

1. The mean is computed with Neumaier (compensated) summation: the error of the sum stays of order $u = 2^{-53}$ instead of growing as $N u$ (§13.2).
2. The mean is subtracted from each sample of a copy.
3. An empty signal is returned unchanged.

Two passes, time $O(N)$, memory $O(N)$.

## With this block's numbers

The DC component of the input $\bar x$ (as a fraction of full scale): {{metric.dc}}. The output is $y[n] = x[n] - ({{metric.dc}})$, and its mean is zero up to rounding. If the offset is large, the signal peaks are asymmetric: on one side there is less headroom to full scale, and removing the offset can fix that.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The test checks the exact result on a simple example: $(1, 2, 3)$ becomes $(-1, 0, 1)$.

## Links

- §0.1 — the DFT: $X[0]$ is the sum of the samples, DC is the zeroth harmonic.
- §1.2 — the z-transform and the DTFT: the value of the spectrum at $\omega = 0$.
- §13.2 — Neumaier compensated summation for long signals.
- When a WAV file is read, the channel statistics (`channel_stats`) compute the same mean $\bar x$ with the same summation.

## Implementation

Compensated mean and subtraction from each sample.

```cpp
std::vector<double> remove_dc(std::span<const double> x) {
    std::vector<double> y(x.begin(), x.end());
    if (y.empty()) return y;
    const double mean = neumaier_sum(x) / static_cast<double>(x.size());
    for (double& v : y) v -= mean;
    return y;
}
```
