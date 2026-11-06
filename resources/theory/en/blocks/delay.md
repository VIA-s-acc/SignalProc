+++
id = "delay"
title = "Delay"
theory = ["conv-signals", "lti-z-dtft", "freq-group-delay"]
source = "src/core/signal_ops.cpp"
function = "delay"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Shift the signal to the right in time by an integer number of samples $d$, prepending zeros. The signal becomes $d$ samples longer; nothing is lost and nothing wraps around.

## Formulas

$$y[n] = \begin{cases} 0, & 0 \le n < d \\ x[n-d], & d \le n < N + d \end{cases}$$

In the $z$-transform and DTFT domains a delay is multiplication by $z^{-d}$:

$$Y(z) = z^{-d}\, X(z), \qquad Y(e^{i\omega}) = e^{-i\omega d}\, X(e^{i\omega})$$

The magnitude spectrum does not change, $\lvert Y \rvert = \lvert X \rvert$, while the phase gets a linear term $-\omega d$. The group delay is constant at all frequencies, $\tau_g(\omega) \equiv d$ samples (§8.4), i.e. the waveform is preserved. In seconds the delay is $d / f_s$.

## Algorithm and cost

1. A vector of $d$ zeros is allocated.
2. The original signal is appended to it.

Time $O(N + d)$, memory $8\,(N + d)$ bytes: a very large delay allocates just as much memory. The delay is integer only; a fractional shift requires interpolation (a filter) and is not performed by this block.

## With this block's numbers

Delay $d = {{block.samples}}$ samples: $y[n] = x[n - {{block.samples}}]$, the first $d$ output samples are zero, and the output length is $N + d$. The time shift is $d / f_s$, and the group delay at all frequencies is $d$.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The test checks the exact result on a simple example: delaying $(1, 2)$ by two samples gives $(0, 0, 1, 2)$.

## Links

- §1.2 — the property $x[n-k] \leftrightarrow z^{-k} X$.
- §8.4 — group delay: a pure delay $z^{-k}$ gives exactly $k$.
- §0.1 — a delay by $k$ samples as multiplication by $z^{-k}$.
- The "Cut a fragment" block — the opposite action: it removes the beginning instead of adding zeros.

## Implementation

A vector of $d$ zeros followed by the signal.

```cpp
std::vector<double> delay(std::span<const double> x, std::size_t samples) {
    std::vector<double> y(samples, 0.0);
    y.insert(y.end(), x.begin(), x.end());
    return y;
}
```
