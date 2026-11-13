# Entropy of a band of quantized coefficients

The signal `x` is `gen` with `kind=doppler`, `rate=1024`, `duration=1`. It is decomposed with `dwt` (`wavelet=db4`, `levels=5`, `ext=periodization`) and the coefficients are quantized by `coeff_quantize` with `mode=bits`, `bits={{b}}`. The `coeff_stats` block computes the Shannon entropy per band, in bits. What is the entropy of the band $d_3$? The tolerance is 0.005 bit.

`coeff_stats` computes the entropy over the energy distribution: $p_i = c_i^2 / \sum_k c_k^2$, $H = -\sum_i p_i \log_2 p_i$. It is the same cost by which `wpt` chooses the best basis.

## Hints {#hints}

1. Build `gen → dwt → coeff_quantize → coeff_stats` and run; in the report the band `d3` has the field `entropy`.
2. The bounds: if all the energy is in one coefficient, $H = 0$; if it is spread equally over all $L$ coefficients of the band, $H = \log_2 L$. The length of $d_3$ at 1024 samples with periodization is 128, so $H \le 7$ bits.
3. Compare with the other bands: the more the energy is gathered in a few coefficients, the smaller $H$ is.

## Solution {#solution}

The entropy of $d_3$ at `bits` = {{b}}: {{expected}} bits.

The energy entropy measures how sparse the wavelet coefficients are: a small $H$ means almost all the energy sits in a few coefficients, and it is enough to keep them in compression ({#audio-compression}). Quantization hardly changes this number: it rounds the coefficients, but large ones stay large, and small ones go to zero and add nothing to the energy. So for the detail bands of a smooth signal $H$ is clearly below the limit $\log_2 L$.
