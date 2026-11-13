# The db2 bank from blocks

Build from separate blocks a two-channel bank with Daubechies db2 filters of length 4: $h_0, h_1$ (`db2.h0`, `db2.h1`), decimation ↓2, expansion ↑2, synthesis $f_0, f_1$ (`db2.f0`, `db2.f1`) and a sum. The bank output is node `sum`; it must return the input `x = [{{x}}]` without distortion, but with a delay. Do not change node `x`. The program names the bank delay itself: `signals filter info db2` prints $\ell$. The check requires the measured delay of the output to be $\ell$ or $\ell - 1$, depending on the decimation phase. Ready-made transforms do not count.

## Goals {#goals}

1. Perfect reconstruction: $\max|\hat x - x| \le 10^{-12}$ allowing for a delay — on your signal and on the hidden ones.
2. The measured delay of $\hat x$ relative to $x$ equals $\ell = 3$ with `phase=0` or $\ell - 1 = 2$ with `phase=1`.
3. Every path from `x` to `sum` goes through decimation ↓2.
4. Every path from `x` to `sum` goes through expansion ↑2.
5. No ready-made wavelet transforms.

## Hints {#hints}

1. Two branches, as in the Haar bank: `x → h0 → ↓2 → ↑2 → f0` and `x → h1 → ↓2 → ↑2 → f1`, then `add`. The filters are `convolve` blocks with `filter=db2.h0` and so on.
2. The built-in lowpass filters of `downsample` and `upsample` are off: `anti_alias=no`, `anti_image=no`.
3. The delay depends on the decimation phase: with `phase=0` the output lags by $\ell$ samples, with `phase=1` (as in PyWavelets, {#conv-pywt}) by $\ell - 1$. Either phase works if both ↓2 use the same one.

## Solution {#solution}

The reconstruction error of the reference solution: {{m.err}}, the measured delay is {{m.lag}} samples.

The Daubechies bank is orthogonal: the synthesis is the reversed analysis, $F_0(z) = H_1(-z)$, $F_1(z) = -H_0(-z)$, and aliasing cancels. What remains is $T(z) = z^{-\ell}$ with $\ell = L - 1 = 3$ ({#fb-distortion-alias}). Decimation with `phase=0` takes $y[2n]$, and the delay is exactly $\ell$; with `phase=1` it takes $y[2n+1]$ and the delay is one sample less, as in task 4.4. The Haar bank also reconstructs the signal, but with a delay of 1 or 0, and the delay goal rejects it.
