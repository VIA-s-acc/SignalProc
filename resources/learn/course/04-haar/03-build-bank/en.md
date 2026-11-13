# Build the Haar bank

Build a two-channel Haar bank from separate blocks: analysis filters $h_0, h_1$, decimation ↓2, expansion ↑2, synthesis filters $f_0, f_1$ and a sum. The bank output is node `sum`; it must return the input `x = [{{x}}]` without distortion. Ready-made transforms (`dwt`, `idwt` and the like) do not count: the bank has to be built by hand. Do not change node `x`: the check feeds other signals into it too.

## Goals {#goals}

1. Perfect reconstruction: $\max|\hat x - x| \le 10^{-12}$ allowing for a delay — on your signal and on the hidden ones.
2. Every path from `x` to `sum` goes through decimation ↓2.
3. Every path from `x` to `sum` goes through expansion ↑2.
4. No ready-made wavelet transforms.

## Hints {#hints}

1. Two branches: `x → h0 → ↓2 → ↑2 → f0` and `x → h1 → ↓2 → ↑2 → f1`, then `add`. Use `convolve` blocks with `filter=haar.h0`, `haar.h1`, `haar.f0`, `haar.f1`.
2. Turn off the built-in lowpass filters of `downsample` and `upsample`: `anti_alias=no` and `anti_image=no`. Otherwise extra filters join the bank and the aliasing does not cancel.
3. Decimation phase `phase=1` keeps the odd samples, like PyWavelets: then the Haar bank reconstructs without delay. With `phase=0` a delay of one sample appears, $\ell = 1$, — the check finds it by itself.

## Solution {#solution}

The graph is the same as in the example `haar_bank.sig`. The reconstruction error of the reference solution: {{m.err}}.

Why it works: after ↓2 and ↑2 each branch carries the signal and its mirror copy $X(-z)$. The Haar synthesis is chosen so that the mirror copies of the two branches cancel, while the direct ones add up to $z^{-\ell}X(z)$ — the distortion and aliasing equations of {#fb-distortion-alias}. Remove the detail branch and the aliasing stays, so reconstruction fails even with the right gain.
