# The delay of the db2 bank

The start graph is the same bank as in the Haar task, but with the Daubechies db2 filters of length 4. The check `pr` with `delay=0` fails: $\hat x$ repeats $x$, but late. By how many samples does $\hat x$ lag behind $x$? The answer is a whole number.

## Hints {#hints}

1. Run the graph (`r`) and compare probe `x̂` with the input: where does the first sample of $x$ show up in $\hat x$?
2. The `check` block can find the delay by itself: set `delay=auto` on `pr`, and the report gets a `delay` field.
3. Theory: for full convolution the bank delay $\ell$ comes from $P_0(z) - P_0(-z) = 2z^{-\ell}$, where $P_0 = F_0 H_0$ has degree $2L - 2 = 6$, so $\ell = 3$. Decimation on odd samples (`phase=1`, as in PyWavelets, {#conv-pywt}) moves everything one sample earlier.

## Solution {#solution}

$\hat x$ lags by {{expected}} samples.

Without decimation the analysis → synthesis chain gives $T(z) = \tfrac12\left[F_0(z)H_0(z) + F_1(z)H_1(z)\right] = z^{-\ell}$ with $\ell = L - 1 = 3$ for filters of length $L = 4$. Decimation with `phase=1` takes the samples $x[2n+1]$, that is, it first advances the signal by one sample, and the expansion puts samples back on even places. So the total delay is $\ell - 1$. For Haar $L = 2$, $\ell = 1$, and the delay is zero — as in task 4.3.
