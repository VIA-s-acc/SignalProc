+++
id = "dwt"
title = "DWT — Mallat pyramid"
theory = ["dwt-mallat", "conv-pywt", "fbm-periodization"]
source = "src/core/dwt.hpp"
function = "dwt_level"
test = "tests/core/test_dwt.cpp"
views = ["decomposition", "matrix", "filters"]
+++

## Purpose

Decompose the signal $x$ into octave bands $d_1, \dots, d_J$ and $a_J$ without losing information.

## Formulas

$$a_j[k] = \sum_n h_0[n]\, a_{j-1}[2k+1-n], \qquad d_j[k] = \sum_n h_1[n]\, a_{j-1}[2k+1-n]$$

$$a_{j-1}[m] = \sum_k \left(f_0[m-2k]\, a_j[k] + f_1[m-2k]\, d_j[k]\right), \qquad a_0 = x$$

The pywt phase: after convolution the odd samples are taken (§0.4) — hence "$2k+1$".

## Algorithm and cost

1. $a_0 \leftarrow x$.
2. For $j = 1, \dots, J$: $a_j = {\downarrow}2\,(h_0 * a_{j-1})$ and $d_j = {\downarrow}2\,(h_1 * a_{j-1})$.
3. Edges — the signal extension mode (§6.4).

Cost $\approx N\,(L_0 + L_1)$ multiplications, memory $O(N)$, at most $\lfloor \log_2 (N/(L-1)) \rfloor$ levels.

## With this block's numbers

$N = {{block.n}}$, filter {{block.wavelet}}: $L_0 = L_1 = {{block.filter_length}}$ ⇒ ≈ {{metric.multiplies}} multiplications per channel; $J = {{block.levels}}$ of $J_{\max} = {{block.max_level}}$, edges — {{block.mode}}.

Band lengths: {{block.band_lengths}}; sum {{block.band_sum}}.

Frequency bands: {{block.band_ranges}}.

## Checks

- Perfect reconstruction on this signal, $\max|\hat x - x|$: {{check.round_trip}}
- Orthonormality $\sum_n h_0[n]\, h_0[n-2k] = \delta[k]$: {{check.orthogonality}}
- Vanishing moments $\sum_n n^p h_1[n] = 0$: {{check.vanishing_moments}}
- Parseval, the energy of the bands equals the energy of the signal: {{check.parseval}}
- Linear phase: {{block.linear_phase}}
- No shift invariance: shifting $x$ by one sample redistributes the energy across levels, hence the SWT (§11.2).

## Links

- §4.1 — the distortion and aliasing equations, where perfect reconstruction comes from.
- §7.3 — the cascade algorithm: which $\varphi$ and $\psi$ the pyramid converges to.
- §11.2 — SWT, the variant without downsampling.
- §6.4 — periodization and the block circulant.

## Implementation

One analysis level: convolution with $h_0$ and $h_1$ and downsampling with the pywt phase.

```cpp
for (std::size_t i = 0; i < len; ++i) {
    const auto top = 2 * static_cast<std::ptrdiff_t>(i) + 1;
    const bool interior = top - (filter_len - 1) >= 0 && top < n;
    T a(0), d(0);
    for (std::ptrdiff_t k = 0; k < filter_len; ++k) {
        const T s = interior ? x[static_cast<std::size_t>(top - k)] : extended_sample(x, top - k, options.mode);
        a += lo[static_cast<std::size_t>(k)] * s;
        d += hi[static_cast<std::size_t>(k)] * s;
    }
    out.approx[i] = a;
    out.detail[i] = d;
}
```
