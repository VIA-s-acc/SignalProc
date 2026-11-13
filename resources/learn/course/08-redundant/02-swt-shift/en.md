# SWT does not mind a shift

Compare two paths. Path $p$: delay `x = [{{x}}]` by one sample (`shift`, circular), then take `swt` and the band $d_1$. Path $q$: take `swt` of the original `x`, pick $d_1$ and then delay that band by one sample in the same way. For a stationary transform without decimation the result is the same. The outputs of the paths are nodes `p` and `q`; the start graph is built on `dwt`, and its paths differ. Replace the blocks so that the paths agree. Do not change node `x`.

Shifts by `shift` with `mode=circular` agree at the edges without reservations. If the delay is done with zeros (`delay`, or `shift` with `mode=zero`), the first samples differ: zeros replace the sample that the circular decomposition would take from the other end. So the check drops two samples at each edge.

## Goals {#goals}

1. The paths agree: $\max|p - q| \le 10^{-12}$ without two samples at each edge — on your signal and on the hidden ones.
2. Every path from `x` to `p` goes through `swt`.
3. Every path from `x` to `q` goes through `swt`.
4. The graph has at least two `swt` blocks and at least two `shift` blocks.

## Hints {#hints}

1. After ↓2 the `dwt` keeps half of the samples, and a shift by one changes which ones stay. You need a block whose bands are as long as the signal: `swt`.
2. Replace the `dwt` blocks with `swt` with the same `wavelet=haar`, `levels=2`; leave `ext` and `norm` of `swt` at their defaults.
3. The order on the paths differs: on $p$ the shift is before `swt`, on $q$ after it. The band $d_1$ is the port `.d1` of `swt`.

## Solution {#solution}

The error of the reference solution: {{m.err}}.

Without decimation $\mathrm{SWT}(z^{-1}X) = z^{-1}\,\mathrm{SWT}(X)$: every band is a convolution of the signal with a filter, and convolution commutes with a delay. Decimation breaks exactly this, because ↓2 does not commute with a delay by one sample. The price is redundancy: every `swt` band is as long as the signal, so there are $J + 1$ times more coefficients ({#swt-modwt}). This invariance is what is needed where the position of an event matters more than the form of the decomposition, for example in denoising.
