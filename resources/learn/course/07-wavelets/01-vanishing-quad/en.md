# A polynomial without details

The signal $x$ holds the values of a polynomial of the second degree $x[n] = \alpha n^2 + \beta n + \gamma$ for $n = 0 \dots 31$: `x = [{{x}}]`. The start graph decomposes it with a one-level `dwt` using the Haar wavelet. Choose the wavelet of block `w` so that the interior coefficients of the detail $d_1$, samples 8 to 11, which node `mid` outputs, are zero to within $10^{-9}$. Do not change nodes `x` and `mid`; the zeros in `mid` must come from the wavelet itself, not from editing the bands.

## Goals {#goals}

1. The largest of the interior coefficients of $d_1$ (node `mid`) is at most $10^{-9}$.
2. The graph keeps a `dwt` block.
3. No blocks that mute or edit bands: `gain`, `affine`, `multiply`, `band_edit`, `threshold`, `denoise`, `keep_largest`, `mra`.

## Hints {#hints}

1. The detail annihilates polynomials of degree below the number of vanishing moments of the wavelet: a wavelet with $p$ vanishing moments does not see polynomials of degree less than $p$; see {#wav-vanishing-moments}.
2. Haar has one vanishing moment and sees only a constant. How many moments does degree 2 need? The command `signals filter info db3` prints the number of zeros at $z = -1$.
3. Wavelets of the family `dbN` have $N$ vanishing moments. Do not take too long ones: the window 8 to 11 must lie away from the edges, and for a wavelet longer than 16 taps the boundary reaches it.

## Solution {#solution}

`db3` fits: it has three zeros at $z = -1$, that is three vanishing moments, and a polynomial of the second degree lies in their space, so inside the signal the detail is zero up to rounding error. `db2` with two moments removes the linear part but leaves the quadratic one, while Haar does not remove even the linear one. Near the edges the detail is not zero: the extension beyond the boundary is no longer a polynomial. That is why the check looks at the interior samples only. More in {#wav-vanishing-moments}.
