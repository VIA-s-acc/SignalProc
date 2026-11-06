+++
id = "mid_side"
title = "Mid/side"
theory = ["audio-stereo", "conv-signals", "met-parseval"]
source = "src/core/signal_ops.cpp"
function = "to_mid_side"
test = "tests/core/test_signal_ops.cpp"
views = ["signal"]
+++

## Purpose

Go from the left and right channels $L$, $R$ to the center (mid, $M$) and the difference (side, $S$) and back. $M$ collects what is the same in both channels, $S$ what makes the channels differ; they can be processed separately, for example denoising $S$ more strongly.

## Formulas

The orthonormal variant is implemented:

$$M[n] = \frac{L[n] + R[n]}{\sqrt{2}}, \qquad S[n] = \frac{L[n] - R[n]}{\sqrt{2}}$$

The inverse transform is the same operation:

$$L[n] = \frac{M[n] + S[n]}{\sqrt{2}}, \qquad R[n] = \frac{M[n] - S[n]}{\sqrt{2}}$$

The transform matrix is $1/\sqrt{2}$ times the rows $(1, 1)$ and $(1, -1)$; it is symmetric and orthogonal, so it is its own inverse, and energy is preserved:

$$M[n]^2 + S[n]^2 = L[n]^2 + R[n]^2$$

The book (§12.6) also gives another variant, $M = \tfrac12 (L + R)$, $S = \tfrac12 (L - R)$, $L = M + S$, $R = M - S$. The implementation uses the orthonormal one: it is well conditioned, and the noise and thresholds in $M$ and $S$ have the same scales as in $L$ and $R$.

For a mono signal ($L = R = x$) one gets $M = \sqrt{2}\, x$ and $S = 0$: the level of $M$ is higher by $20 \log_{10} \sqrt{2} \approx 3.01$ dB, and the peak may exceed full scale. For channels in antiphase ($R = -L$), $M = 0$.

## Algorithm and cost

1. It is checked that there are exactly two channels of the same length; otherwise an error.
2. For each frame the sum and the difference of the samples are computed, multiplied by $c = \sqrt{2}/2 \approx 0.7071$.
3. The result is written into two channels with the same sampling rate.

Time $O(N)$, memory $O(N)$. The forward and inverse transforms are the same code, so applying it twice returns the input up to rounding errors of order $10^{-16}$.

## With this block's numbers

The input signal has $C = {{block.channels}}$ channels; the transform is defined only for $C = 2$, and any other number of channels gives an error. The coefficient $c = 1/\sqrt{2} \approx 0.7071$ (that is $-3.01$ dB) is the same in both directions, so the sum of the energies of $M$ and $S$ equals the sum of the energies of $L$ and $R$.

## Checks

This block has no numerical check in the interface: its correctness is pinned down by tests.

On a random stereo signal the test checks that the double transform returns the input with an error of at most $4 \cdot 10^{-16}$, and that the total energy is preserved with a relative error of $10^{-12}$; signals with one or three channels are rejected with an error.

## Links

- §12.6 — stereo: mid/side, separate denoising of $M$ and $S$, a shared mask.
- §14.3 — Parseval: an orthogonal transform preserves energy.
- §0.3 — $\sqrt{2}$ normalization instead of the summing one: the same choice as for filter banks.
- The "Mix down to mono" block is $M$ with weight $\tfrac12$ instead of $1/\sqrt{2}$.

## Implementation

A 45-degree rotation of the channel pair: `to_mid_side` and `from_mid_side` call the same function.

```cpp
AudioBuffer rotate_stereo(const AudioBuffer& audio) {
    constexpr double c = std::numbers::sqrt2 / 2;
    AudioBuffer out{audio.sample_rate, {audio.channels[0], audio.channels[1]}};
    for (std::size_t k = 0; k < audio.frames(); ++k) {
        const double a = audio.channels[0][k], b = audio.channels[1][k];
        out.channels[0][k] = (a + b) * c;
        out.channels[1][k] = (a - b) * c;
    }
    return out;
}
```
