# Haar: coefficients a by hand

One Haar analysis step: the signal `x = [{{x}}]` goes through the filter $h_0$ (full convolution, $h_0 = [\tfrac{1}{\sqrt2}, \tfrac{1}{\sqrt2}]$), then the samples at odd places are kept: $\downarrow 2$ with `phase=1`, as in the program and in PyWavelets ({#conv-pywt}). Find the approximation coefficients $a$. The answer is a vector of numbers separated by spaces; a number may be an expression such as `sqrt(2)/2`. Tolerance $10^{-9}$.

## Hints {#hints}

1. The full convolution with $h_0$ gives $y[k] = \tfrac{1}{\sqrt2}\,(x[k] + x[k-1])$, where $x[-1] = 0$.
2. `phase=1` keeps $y[1], y[3], y[5], y[7]$: each of them sums two neighbours with no zero edge.
3. So $a[n]$ depends only on $x[2n]$ and $x[2n+1]$. A check: $\sum a^2$ is at most $\sum x^2$, and together with the details of task 4.2 they are equal.

## Solution {#solution}

$a = [{{expected}}]$.

$a[n] = (x[2n] + x[2n+1])/\sqrt2$: each pair of neighbours is replaced by its sum divided by $\sqrt2$. This is the step of {#haar-walkthrough}. Phase 1 is chosen so that the output has no edge samples with half a pair: the convolution is one sample longer than the input, and the extra tail is dropped at odd phase.
