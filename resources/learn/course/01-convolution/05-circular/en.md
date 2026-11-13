# Circular convolution

Find the circular convolution of length $N = 4$ of the signal `x = [{{x}}]` with the filter `h = [{{h}}]`: indices are taken modulo $N$, so the tail of the ordinary convolution wraps around to the start. The answer is $N = 4$ numbers separated by spaces.

## Hints {#hints}

1. Circular convolution is multiplication by a circulant: a matrix whose every column is the previous one cyclically shifted down by a row. See {#toeplitz-circulant}.
2. First compute the full convolution $y$ of $N + L - 1 = 6$ samples, as in task 1.1.
3. Add the samples of the full convolution with numbers $N, N+1, \dots$ to those with numbers $0, 1, \dots$: $y_c[n] = y[n] + y[n + N]$.

## Solution {#solution}

$y_c = [{{expected}}]$. The full convolution is $L - 1 = 2$ samples longer than $N$. Circular convolution lives on a ring of $N$ positions, so the extra samples $y[4]$ and $y[5]$ land on positions 0 and 1 and add to $y[0]$ and $y[1]$. The sum of the samples does not change: it is still $\left(\sum x\right)\left(\sum h\right)$. The program has no circular convolution block; the reference graph builds it from `fir_taps`, `cut`, `pad` and `add`.
