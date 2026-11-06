+++
id = "idwt"
title = "IDWT — inverse pyramid"
theory = ["dwt-mallat", "fb-distortion-alias", "conv-pywt", "fbm-boundary-modes"]
source = "src/core/dwt.hpp"
function = "idwt_level"
test = "tests/core/test_dwt.cpp"
views = ["decomposition"]
+++

## Purpose

Assemble the signal back from the bands $a_J, d_J, \dots, d_1$: at each level insert zeros, filter with the synthesis filters $f_0, f_1$ and add. If the bank has PR, the result coincides with the original signal.

## Formulas

$$a_{j-1}[m] = \sum_k \left(f_0[m-2k]\, a_j[k] + f_1[m-2k]\, d_j[k]\right), \qquad j = J, J-1, \dots, 1, \qquad \hat x = a_0$$

The pywt phase (§0.4) trims $L-2$ samples from each edge of the full convolution: output sample $i$ is assembled from the terms with $m = i + L - 2$, and the result length for $n$ coefficients in a band is

$$n_{\mathrm{out}} = 2n - L + 2$$

In `periodization` mode the result length is exactly $2n$.

The condition for perfect reconstruction is zero aliasing and a pure delay (§4.1): $A \equiv 0$, $T = z^{-\ell}$.

## Algorithm and cost

1. Start with $a_J$ and $d_J$.
2. For $j = J, \dots, 1$: if $a_j$ is one sample longer than $d_j$ (an odd length in the forward pass), drop the last sample of $a_j$; then $a_{j-1} = \mathrm{idwt}(a_j, d_j)$.
3. Trim the result to the original length $N$.

The zeros of $\uparrow 2$ are not materialized: for an output sample only the taps with step two are summed. Cost $\approx N\,(L_0 + L_1)$ multiplications, memory $O(N)$ — the same as the forward pyramid. The sign of the synthesis highpass filter is determined by the pywt compatibility setting.

## With this block's numbers

Bank {{block.wavelet}}, $J = {{block.levels}}$ levels, edges — {{block.mode}}: $N = {{block.n}}$ samples are reconstructed from the bands in $J = {{block.levels}}$ synthesis passes.

## Checks

- Perfect reconstruction on this signal, $\max\lvert \hat x - x \rvert$: {{check.round_trip}}
- `idwt(dwt(x))` equals `x` in all nine edge modes for orthogonal and biorthogonal banks (§6.5).
- Single-sample signals are returned in the modes where they are allowed; a length mismatch between approximation and details is an error (test).

## Links

- §4.1 — the distortion and aliasing equations, where perfect reconstruction comes from.
- §0.4 — highpass signs and the pywt phase.
- §6.4 — periodization: synthesis by a block circulant, length exactly $2n$.
- §6.5 — edge modes and output lengths.
- §11.1 — the Mallat pyramid: the forward pass and the band layout.

## Implementation

One synthesis level: for each output sample the contributions of the approximation and the details are summed with step two.

```cpp
    for (std::ptrdiff_t i = 0; i < out_len; ++i) {
        if (stop_requested(progress, static_cast<std::size_t>(i))) return std::unexpected{cancelled_error("idwt")};
        const auto pos = i + filter_len - 2;
        T acc(0);
        for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, (pos - filter_len + 2) / 2); k < n; ++k) {
            const auto j = pos - 2 * k;
            if (j < 0) break;
            if (j >= filter_len) continue;
            acc += lo[static_cast<std::size_t>(j)] * approx[static_cast<std::size_t>(k)] +
                   hi[static_cast<std::size_t>(j)] * detail[static_cast<std::size_t>(k)];
        }
        out[static_cast<std::size_t>(i)] = acc;
    }
```
