# The delay ℓ from a halfband P₀

After the choice of synthesis $F_0(z) = H_1(-z)$, $F_1(z) = -H_0(-z)$, perfect reconstruction is equivalent to $P_0(z) - P_0(-z) = 2z^{-\ell}$ for $P_0 = F_0 H_0$ ({#fb-alias-cancellation}). Let $P_0$ be a symmetric polynomial of degree {{deg}} in $z^{-1}$ (its coefficients $p_0[k] = p_0[{{deg}} - k]$, full convolution, no decimation). What is the delay $\ell$? The answer is a whole number.

## Hints {#hints}

1. The condition means: $P_0$ has exactly one nonzero coefficient with an odd index, $p_0[\ell] = 1$; the even coefficients are arbitrary.
2. The symmetry $p_0[k] = p_0[{{deg}} - k]$ maps an odd coefficient to an odd one. The only odd coefficient must map to itself.
3. So it sits in the centre of the polynomial. Compare with the db2 bank of task 4.4: there the degree is 6, and the bank lags by $\ell - 1$ samples, not $\ell$ — because of the decimation phase.

## Solution {#solution}

$\ell = {{expected}}$.

A symmetric polynomial of degree $n$ is symmetric about index $n/2$, so the only odd coefficient sits in the centre: $\ell = n/2$, and $n/2$ must be odd. For $n = 6$ this is $\ell = 3$: so for db2 and for every filter of length $L = 4$, where $n = 2L - 2$ and $\ell = L - 1$. In task 4.4 the measured delay is 2 — one sample less, because decimation with `phase=1` takes $x[2n+1]$ and so advances the signal; without the phase shift (`phase=0`) the measured delay equals $\ell$ ({#fb-distortion-alias}).
