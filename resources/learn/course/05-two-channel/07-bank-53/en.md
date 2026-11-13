# The 5/3 bank from blocks

Build from separate blocks a two-channel linear-phase 5/3 bank (`bior2.2`): analysis $h_0, h_1$ (`bior2.2.h0`, `bior2.2.h1`), decimation ↓2, expansion ↑2, synthesis $f_0, f_1$ (`bior2.2.f0`, `bior2.2.f1`) and a sum. The bank output is node `sum`; it must return the input `x = [{{x}}]` without distortion, but with a delay. Do not change node `x`. The required delay is printed by `signals filter info bior2.2`. Ready-made transforms (`lift53` included) do not count.

## Goals {#goals}

1. Perfect reconstruction: $\max|\hat x - x| \le 10^{-12}$ allowing for a delay — on your signal and on the hidden ones.
2. The measured delay of $\hat x$ relative to $x$ equals $\ell = 5$ with `phase=0` or $\ell - 1 = 4$ with `phase=1`.
3. Every path from `x` to `sum` goes through decimation ↓2.
4. Every path from `x` to `sum` goes through expansion ↑2.
5. No ready-made wavelet transforms.

## Hints {#hints}

1. The structure is the same as in the Haar and db2 banks: two branches `convolve → ↓2 → ↑2 → convolve` and `add`. Only the filter names change.
2. The built-in lowpass filters of `downsample` and `upsample` are off: `anti_alias=no`, `anti_image=no`.
3. With `phase=0` on both ↓2 the delay equals $\ell$, with `phase=1` (as in PyWavelets) it is one sample less ({#conv-pywt}). Both count.

## Solution {#solution}

The reconstruction error of the reference solution: {{m.err}}, the measured delay is {{m.lag}} samples.

The 5/3 bank is not orthogonal: the synthesis is not the reversed analysis, but $F_0(z) = H_1(-z)$, $F_1(z) = -H_0(-z)$ still cancel the aliasing, and $P_0(z) - P_0(-z) = 2z^{-\ell}$ gives $\ell = 5$ ({#fb-alias-cancellation}). The Haar bank also reconstructs the signal, but with a delay of 1 or 0, and the delay goal rejects it; a graph with `lift53` is rejected by the ban on ready-made blocks.
