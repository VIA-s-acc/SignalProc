+++
id = "reverse"
title = "Time reversal"
theory = ["conv-signals", "lti-z-dtft", "cls-filtfilt"]
source = "src/core/signal_ops.cpp"
function = "reverse"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Play the signal backwards: the first sample becomes the last. Useful for listening checks (reversal hides the attack and exposes the decay), for backward filtering and as a building block of forward-backward zero-phase filtering.

## Formulas

$$y[n] = x[N-1-n], \qquad n = 0, \dots, N-1$$

For a real $x$ in the $z$-transform and DTFT domains:

$$Y(z) = z^{-(N-1)}\, X(z^{-1}), \qquad Y(e^{i\omega}) = e^{-i\omega (N-1)}\, \overline{X(e^{i\omega})}$$

The magnitude spectrum does not change, $\lvert Y \rvert = \lvert X \rvert$, while the phase changes sign and gets a linear term: $\arg Y = -\arg X - \omega (N-1)$. Energy, peak, RMS and the DC component stay the same, and the operation is its own inverse: reversing twice returns the input.

The book writes forward-backward zero-phase filtering in terms of the reversal $R$ (§9.12):

$$y = R\big( h * R(h * x) \big), \qquad \lvert H(e^{i\omega}) \rvert^2, \ \text{zero phase}$$

## Algorithm and cost

1. A new vector is built from iterators running from the end of the input to its start.

Time $O(N)$, memory $O(N)$, one copy in reverse order. No values are recomputed, so the reversal is exact, with no rounding.

## With this block's numbers

The block has no parameters of its own: the result depends only on the input. The output has the same length $N$ and the same sampling rate as the input; the first output sample is the last input sample.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

The test checks the exact result on a simple example: reversing $(1, 2, 3)$ gives $(3, 2, 1)$.

## Links

- §0.1 — time reversal $h^-[n] = h[-n]$ corresponds to $H(z^{-1})$.
- §1.2 — the property $x[-n] \leftrightarrow X(z^{-1})$.
- §9.12 — forward-backward zero-phase filtering: two reversals and two passes of the same filter.
- The "Delay" block — another operation on the time axis: a shift instead of a reflection.

## Implementation

Reversal is the one-line `reverse` (copying from the end to the start); it is shown together with its neighbor in the file, `delay`.

```cpp
std::vector<double> delay(std::span<const double> x, std::size_t samples) {
    std::vector<double> y(samples, 0.0);
    y.insert(y.end(), x.begin(), x.end());
    return y;
}

std::vector<double> reverse(std::span<const double> x) { return {x.rbegin(), x.rend()}; }
```
