# Only a₃ and d₃

The signal `x = [{{x}}]` is decomposed by a three-level `dwt` with the wavelet `{{wv}}` and periodization, and an `idwt` block assembles it back. The output of the graph is node `y`. Make `y` a signal assembled only from two bands: the approximation $a_3$ and the detail $d_3$; the other bands $d_2$ and $d_1$ must be zeroed. The reference is exactly such a signal; the check compares with it to within $10^{-12}$ on your input and on hidden ones. Do not change nodes `x` and `w`.

## Goals {#goals}

1. The output `y` matches the reference assembled from $a_3$ and $d_3$ to within $10^{-12}$, on your signal and on the hidden ones.
2. The graph has `dwt` and `idwt` blocks.

## Hints {#hints}

1. A `band_edit` block between `dwt` and `idwt` keeps the listed bands and zeroes the rest. In the `enabled` parameter the bands are listed separated by commas.
2. The bands of a three-level decomposition are called $a_3, d_3, d_2, d_1$. You need the first two; for the general case see {#dwt-mallat}.
3. If you keep all the bands, the output is the input `x` itself, not the reference: the zeros in $d_2$ and $d_1$ change the signal. The check will notice.

## Solution {#solution}

`band_edit(enabled="a3,d3")` between `dwt` and `idwt`. The inverse transform is linear, so the output is the sum of the contributions of the separate bands: $y = \mathrm{mra}_{a_3}(x) + \mathrm{mra}_{d_3}(x)$. The contributions of $d_2$ and $d_1$, the high-frequency components, vanish, leaving a smoothed version of $x$ with details of the third level only. Error against the reference: {{m.err}}. Keeping all bands restores the signal fully, but that is no longer the reference. More in {#dwt-mallat} and {#wav-mra}.
